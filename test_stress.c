#include "unified_shard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_INSERTS 10000

int main() {
    printf("[Stress Test] Initializing Unified Engine...\n");
    UnifiedEngine *engine = malloc(sizeof(UnifiedEngine));
    if (!engine) {
        fprintf(stderr, "Failed to allocate UnifiedEngine (size: %zu bytes)\n", sizeof(UnifiedEngine));
        return 1;
    }
    if (engine_init(engine, "./fts_uring_data", 1, 6144) != 0) {
        fprintf(stderr, "Failed to initialize engine.\n");
        return 1;
    }

    printf("[Stress Test] Inserting %d documents...\n", NUM_INSERTS);
    clock_t start = clock();

    for (int i = 0; i < NUM_INSERTS; i++) {
        uint8_t node_id[16] = {0};
        snprintf((char*)node_id, 16, "node_%d", i);

        uint32_t tf[MAX_FTS_FIELDS] = {0};
        tf[0] = 1; // One term in field 0

        uint32_t dl[MAX_FTS_FIELDS] = {0};
        dl[0] = 5; 

        float embedding[EMBEDDING_DIM] = {0};
        embedding[0] = 0.1f;

        uint32_t term_ids[1] = {12345};
        
        if (shard_insert_document(&engine->shards[0], &engine->shards[0].wal, node_id, tf, dl, embedding, term_ids, 1) != 0) {
            fprintf(stderr, "Failed to insert document %d\n", i);
            break;
        }

        if (i % 10000 == 0 && i > 0) {
            printf("  Inserted %d documents...\n", i);
        }
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("[Stress Test] Completed %d inserts in %f seconds (%.2f docs/sec).\n", 
            NUM_INSERTS, time_taken, NUM_INSERTS / time_taken);

    printf("[Stress Test] Shutting down engine (flushing buffers)...\n");
    engine_close(engine);
    free(engine);

    return 0;
}
