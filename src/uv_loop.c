#include "../include/fts_types.h"
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uv_loop_t *loop;

typedef struct {
    uv_fs_t req;
    int shard_idx;
    char *buffer;
    double idf;
    double avgdl;
    double k1;
    double b;
} ShardReadContext;

// Callback triggered when the asynchronous read completes
void on_read_cb(uv_fs_t *req) {
    ShardReadContext *ctx = (ShardReadContext *)req->data;
    if (req->result < 0) {
        fprintf(stderr, "Async read failed for shard %d: %s\n", ctx->shard_idx, uv_strerror(req->result));
    } else if (req->result == 0) {
        printf("EOF on shard %d\n", ctx->shard_idx);
    } else {
        printf("Successfully read %zd bytes from shard %d:\n", req->result, ctx->shard_idx);
        // Deserializes posting payload and computes BM25 score
        process_shard_buffer(ctx->buffer, (size_t)req->result, ctx->idf, ctx->avgdl, ctx->k1, ctx->b);
    }
    
    uv_fs_req_cleanup(req);
    free(ctx);
}

int fts_init_uv() {
    loop = uv_default_loop();
    if (!loop) {
        fprintf(stderr, "Failed to initialize default libuv loop\n");
        return -1;
    }
    return 0;
}

void fts_cleanup_uv() {
    uv_loop_close(loop);
}

// Submits asynchronous reads for all shards to the libuv event loop
void scatter_query_to_shards_uv(int *shard_fds, int num_shards, char **buffers, size_t read_size, double idf, double avgdl, double k1, double b) {
    for (int i = 0; i < num_shards; i++) {
        ShardReadContext *ctx = malloc(sizeof(ShardReadContext));
        ctx->req.data = ctx;
        ctx->shard_idx = i;
        ctx->buffer = buffers[i];
        ctx->idf = idf;
        ctx->avgdl = avgdl;
        ctx->k1 = k1;
        ctx->b = b;
        
        uv_buf_t iov = uv_buf_init(buffers[i], read_size);
        uv_fs_read(loop, &ctx->req, shard_fds[i], &iov, 1, 0, on_read_cb);
    }
}

void gather_shard_results_uv() {
    uv_run(loop, UV_RUN_DEFAULT);
}
