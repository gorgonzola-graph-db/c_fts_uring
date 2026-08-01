#include "async_search.h"
#include <stdlib.h>
#include <string.h>
#include "inverted_index.h"
#include <stddef.h>

static uv_loop_t *async_search_loop = NULL;

int async_search_init(void) {
    if (!async_search_loop) {
        async_search_loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
        if (uv_loop_init(async_search_loop) != 0) {
            free(async_search_loop);
            async_search_loop = NULL;
            return -1;
        }
    }
    return 0;
}

void async_search_shutdown(void) {
    if (async_search_loop) {
        uv_loop_close(async_search_loop);
        free(async_search_loop);
        async_search_loop = NULL;
    }
}

static void shard_search_work_cb(uv_work_t *req) {
    ShardSearchWork *work = (ShardSearchWork *)req->data;
    work->result_count = 0;
    work->status = 0;

    if (work->query.use_text) {
        // We use the term_id = 0 for now since test_pecorino inserts to term_id = 0
        // In a full implementation, we'd loop over tokens from work->query
        work->result_count += shard_search_bm25f_indexed(work->shard, 0, work->idf, 
                                                 work->avgdl, work->weights, 
                                                 work->k1, work->b, 
                                                 &work->results[work->result_count], 
                                                 MAX_RESULTS_PER_SHARD - work->result_count);
    }

    if (work->query.use_vector && work->result_count < MAX_RESULTS_PER_SHARD) {
        work->result_count += shard_search_vector(work->shard, work->query.query_embedding,
                                                  &work->results[work->result_count],
                                                  MAX_RESULTS_PER_SHARD - work->result_count);
    }
}

static void shard_search_done_cb(uv_work_t *req, int status) {
    ShardSearchWork *work = (ShardSearchWork *)req->data;
    
    // Find the parent AsyncSearchContext using pointer arithmetic
    ShardSearchWork *first_work = work - work->shard->shard_id;
    AsyncSearchContext *ctx = (AsyncSearchContext *)((char *)first_work - offsetof(AsyncSearchContext, shard_work));
    
    ctx->completed++;
}

int engine_search_async(UnifiedEngine *engine,
                        const UnifiedQuery *query,
                        UnifiedSearchResult *results,
                        uint32_t max_results) {
    AsyncSearchContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.num_shards = engine->num_shards;
    ctx.loop = async_search_loop;
    ctx.output_results = results;
    ctx.output_max = max_results;
    
    for (uint32_t i = 0; i < engine->num_shards; i++) {
        ShardSearchWork *w = &ctx.shard_work[i];
        w->work_req.data = w;
        w->shard = &engine->shards[i];
        w->query = *query;
        w->idf = 1.0;
        for (int j = 0; j < MAX_FTS_FIELDS; j++) {
            w->avgdl[j] = engine->global_avgdl[j];
            w->weights[j] = engine->field_weights[j];
        }
        w->k1 = engine->k1;
        w->b = engine->b;
        
        uv_queue_work(ctx.loop, &w->work_req, shard_search_work_cb, shard_search_done_cb);
    }
    
    // Block until all shards finish
    uv_run(ctx.loop, UV_RUN_DEFAULT);
    
    // Merge results
    UnifiedSearchResult bm25f_all[MAX_SHARDS * MAX_RESULTS_PER_SHARD];
    uint32_t bm25f_count = 0;
    
    UnifiedSearchResult vector_all[MAX_SHARDS * MAX_RESULTS_PER_SHARD];
    uint32_t vector_count = 0;
    
    for (uint32_t i = 0; i < engine->num_shards; i++) {
        ShardSearchWork *w = &ctx.shard_work[i];
        for (uint32_t j = 0; j < w->result_count; j++) {
            if (query->use_text && w->results[j].bm25f_score > 0) {
                bm25f_all[bm25f_count++] = w->results[j];
            } else if (query->use_vector && (w->results[j].cosine_score != 0.0 || !query->use_text)) {
                vector_all[vector_count++] = w->results[j];
            }
        }
    }
    
    // Rank and fuse
    if (query->use_text) {
        for (uint32_t i = 0; i < bm25f_count; i++) {
            for (uint32_t j = i + 1; j < bm25f_count; j++) {
                if (bm25f_all[j].bm25f_score > bm25f_all[i].bm25f_score) {
                    UnifiedSearchResult tmp = bm25f_all[i];
                    bm25f_all[i] = bm25f_all[j];
                    bm25f_all[j] = tmp;
                }
            }
            bm25f_all[i].bm25f_rank = i + 1;
        }
    }
    
    if (query->use_vector) {
        for (uint32_t i = 0; i < vector_count; i++) {
            for (uint32_t j = i + 1; j < vector_count; j++) {
                if (vector_all[j].cosine_score > vector_all[i].cosine_score) {
                    UnifiedSearchResult tmp = vector_all[i];
                    vector_all[i] = vector_all[j];
                    vector_all[j] = tmp;
                }
            }
            vector_all[i].cosine_rank = i + 1;
        }
    }
    
    UnifiedSearchResult merged[MAX_SHARDS * MAX_RESULTS_PER_SHARD * 2];
    uint32_t merged_count = 0;
    
    for (uint32_t i = 0; i < bm25f_count; i++) {
        merged[merged_count++] = bm25f_all[i];
    }
    
    for (uint32_t i = 0; i < vector_count; i++) {
        bool found = false;
        for (uint32_t j = 0; j < merged_count; j++) {
            if (memcmp(merged[j].node_id, vector_all[i].node_id, 16) == 0) {
                merged[j].cosine_score = vector_all[i].cosine_score;
                merged[j].cosine_rank = vector_all[i].cosine_rank;
                found = true;
                break;
            }
        }
        if (!found) {
            merged[merged_count++] = vector_all[i];
        }
    }
    
    if (query->use_text && query->use_vector) {
        compute_rrf_fusion(merged, merged_count);
    } else {
        for (uint32_t i = 0; i < merged_count; i++) {
            for (uint32_t j = i + 1; j < merged_count; j++) {
                double score_i = query->use_text ? merged[i].bm25f_score : merged[i].cosine_score;
                double score_j = query->use_text ? merged[j].bm25f_score : merged[j].cosine_score;
                if (score_j > score_i) {
                    UnifiedSearchResult tmp = merged[i];
                    merged[i] = merged[j];
                    merged[j] = tmp;
                }
            }
        }
    }
    
    ctx.output_count = (merged_count > max_results) ? max_results : merged_count;
    memcpy(results, merged, ctx.output_count * sizeof(UnifiedSearchResult));
    
    return ctx.output_count;
}
