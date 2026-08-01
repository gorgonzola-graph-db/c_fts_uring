#ifndef ASYNC_SEARCH_H
#define ASYNC_SEARCH_H

#include "unified_shard.h"
#include <uv.h>

// Per-shard work context for the thread pool
typedef struct {
    uv_work_t work_req;
    UnifiedShard *shard;
    UnifiedQuery query;
    UnifiedSearchResult results[MAX_RESULTS_PER_SHARD];
    uint32_t result_count;
    
    // BM25F parameters (copied from engine)
    double idf;
    double avgdl[MAX_FTS_FIELDS];
    double weights[MAX_FTS_FIELDS];
    double k1;
    double b;
    
    // Completion tracking
    int status;  // 0 = success, -1 = error
} ShardSearchWork;

// Async scatter-gather context
typedef struct {
    ShardSearchWork shard_work[MAX_SHARDS];
    uint32_t num_shards;
    uint32_t completed;
    uv_loop_t *loop;
    
    // Final merged results
    UnifiedSearchResult *output_results;
    uint32_t output_max;
    int output_count;  // Set when all shards complete
} AsyncSearchContext;

// Initialize the async search subsystem (creates a libuv loop)
int async_search_init(void);

// Shutdown the async search subsystem
void async_search_shutdown(void);

// Scatter query across all shards using libuv thread pool, gather and fuse results.
// This is the async replacement for engine_search().
// Returns number of results written to output_results.
int engine_search_async(UnifiedEngine *engine,
                        const UnifiedQuery *query,
                        UnifiedSearchResult *results,
                        uint32_t max_results);

#endif // ASYNC_SEARCH_H
