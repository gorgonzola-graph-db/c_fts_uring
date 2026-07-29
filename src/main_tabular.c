#include "catalog.h"
#include "slotted_page.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define NUM_SHARDS 4

typedef struct {
    uint32_t id;
    int32_t complexity;
    double score;
} MetricTuple;

typedef struct {
    uv_fs_t req;
    int shard_idx;
    char *buffer;
    TableSchema schema;
} TabularReadContext;

static uv_loop_t *loop;
static uint64_t global_total_count = 0;
static double global_total_sum = 0.0;

static void on_tabular_page_read(uv_fs_t *req) {
    TabularReadContext *ctx = (TabularReadContext *)req->data;
    if (req->result < 0) {
        fprintf(stderr, "Async tabular read failed on shard %d: %s\n", ctx->shard_idx, uv_strerror(req->result));
    } else if (req->result > 0) {
        typedef struct { uint64_t matching_rows; double aggregated_sum; } PartialAggregate;
        extern PartialAggregate process_tabular_page(const char *page_buf, const TableSchema *schema, uint32_t filter_col_idx, double min_val_filter, uint32_t agg_col_idx);

        // Run partial aggregation on this shard: WHERE complexity > 10, AGGREGATE SUM(score)
        PartialAggregate partial = process_tabular_page(ctx->buffer, &ctx->schema, 1, 10.0, 2);

        printf("  [Shard %d Executed] Matched Rows: %lu, Partial Sum: %.2f\n", 
               ctx->shard_idx, partial.matching_rows, partial.aggregated_sum);

        global_total_count += partial.matching_rows;
        global_total_sum += partial.aggregated_sum;
    }

    uv_fs_req_cleanup(req);
    free(ctx->buffer);
    free(ctx);
}

int main() {
    printf("===================================================================\n");
    printf("Custom C Async Tabular Relational DB Engine (Pruning + Scatter-Gather)\n");
    printf("===================================================================\n");

    loop = uv_default_loop();

    // 1. Initialize Global Catalog & Schema Definition
    Catalog cat;
    catalog_init_table(&cat, "code_metrics");
    catalog_add_column(&cat, "id", TYPE_INT32, sizeof(uint32_t));
    catalog_add_column(&cat, "complexity", TYPE_INT32, sizeof(int32_t));
    catalog_add_column(&cat, "score", TYPE_FLOAT64, sizeof(double));

    printf("Catalog Registered Table '%s' (Columns: %u, Tuple Size: %u bytes)\n\n",
           cat.schema.table_name, cat.schema.column_count, cat.schema.tuple_size);

    // 2. Provision Slotted Page Shard Files with Key Range Metadata
    int shard_fds[NUM_SHARDS];
    for (int i = 0; i < NUM_SHARDS; i++) {
        char filename[64];
        snprintf(filename, sizeof(filename), "/dev/shm/tab_shard_%d.dat", i);
        int32_t min_k = i * 100;
        int32_t max_k = (i + 1) * 100 - 1;
        catalog_add_shard(&cat, i, filename, min_k, max_k, 5);

        int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Failed to create table shard file");
            return EXIT_FAILURE;
        }

        char page_buf[PAGE_SIZE];
        slotted_page_init(page_buf, (uint32_t)i, PAGE_TYPE_ROW);

        for (int r = 0; r < 5; r++) {
            MetricTuple tuple;
            tuple.id = min_k + r * 20; // e.g., Shard 0: 0..80, Shard 1: 100..180, Shard 2: 200..280
            tuple.complexity = (r % 2 == 0) ? 15 + r : 5 + r;
            tuple.score = 25.5 * (r + 1);

            slotted_page_insert_tuple(page_buf, &tuple, sizeof(MetricTuple));
        }

        write(fd, page_buf, PAGE_SIZE);
        close(fd);

        shard_fds[i] = open(filename, O_RDONLY);
    }

    // 3. Query Planner Phase: Shard Pruning & Predicate Routing
    int32_t query_min_key = 120;
    int32_t query_max_key = 250;
    printf("Query: SELECT COUNT(*), SUM(score) FROM code_metrics WHERE id BETWEEN %d AND %d AND complexity > 10\n\n",
           query_min_key, query_max_key);
    printf("--- Query Planner & Shard Pruning Phase ---\n");

    uint32_t scanned_shards = 0;
    uint32_t pruned_shards = 0;

    for (uint32_t i = 0; i < cat.shard_count; i++) {
        const ShardMapEntry *shard = &cat.shards[i];

        if (catalog_should_scan_shard(shard, query_min_key, query_max_key)) {
            printf("  [Keep Shard %u] Key Range [%d..%d] intersects query [%d..%d] -> Dispatching async I/O\n",
                   shard->shard_id, shard->min_key, shard->max_key, query_min_key, query_max_key);

            TabularReadContext *ctx = malloc(sizeof(TabularReadContext));
            ctx->req.data = ctx;
            ctx->shard_idx = i;
            ctx->buffer = malloc(PAGE_SIZE);
            ctx->schema = cat.schema;

            uv_buf_t iov = uv_buf_init(ctx->buffer, PAGE_SIZE);
            uv_fs_read(loop, &ctx->req, shard_fds[i], &iov, 1, 0, on_tabular_page_read);
            scanned_shards++;
        } else {
            printf("  [PRUNED Shard %u] Key Range [%d..%d] outside query [%d..%d] -> Zero I/O dispatched\n",
                   shard->shard_id, shard->min_key, shard->max_key, query_min_key, query_max_key);
            pruned_shards++;
        }
    }

    printf("\nPlanner Summary: Dispatched I/O to %u shards, Pruned %u shards completely.\n\n",
           scanned_shards, pruned_shards);

    // 4. Run Event Loop for Active Scattered Shards
    printf("--- Async Scatter-Gather Gathering Phase ---\n");
    uv_run(loop, UV_RUN_DEFAULT);

    printf("\n===================================================================\n");
    printf("FINAL TABULAR AGGREGATE RESULTS:\n");
    printf("  Total Rows Matched (COUNT): %lu\n", global_total_count);
    printf("  Total Accumulated Score (SUM): %.2f\n", global_total_sum);
    printf("===================================================================\n");

    // Cleanup
    for (int i = 0; i < NUM_SHARDS; i++) {
        close(shard_fds[i]);
        char filename[64];
        snprintf(filename, sizeof(filename), "/dev/shm/tab_shard_%d.dat", i);
        unlink(filename);
    }

    uv_loop_close(loop);
    return EXIT_SUCCESS;
}
