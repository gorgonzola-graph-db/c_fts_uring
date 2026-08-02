#include "include/unified_shard.h"
#include "include/db_engine.h"
#include "include/async_search.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    system("rm -rf ./fts_uring_data_test");
    printf("[1] Initializing Unified Engine...\n");
    UnifiedEngine *engine = malloc(sizeof(UnifiedEngine));
    if (engine_init(engine, "./fts_uring_data_test", 1, 1024) != 0) {
        fprintf(stderr, "Failed to init engine\n");
        return 1;
    }
    async_search_init();
    
    printf("[2] Inserting document with terms {10, 20, 30}...\n");
    uint8_t node_id[16] = {
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22,
        0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00
    };
    
    uint32_t tf[MAX_FTS_FIELDS] = {10, 0, 0, 0}; 
    uint32_t dl[MAX_FTS_FIELDS] = {3, 0, 0, 0};
    float embedding[EMBEDDING_DIM] = {0.0};
    uint32_t term_ids[3] = {10, 20, 30};
    if (engine_insert_document(engine, node_id, tf, dl, embedding, term_ids, 3) != 0) {
        fprintf(stderr, "Failed to insert document\n");
    }
    
    printf("[3] Querying exact phrase {10, 20, 30}...\n");
    UnifiedQuery query;
    memset(&query, 0, sizeof(query));
    query.is_phrase = true;
    query.use_text = true;
    query.use_vector = false;
    query.num_terms = 3;
    query.term_ids[0] = 10;
    query.term_ids[1] = 20;
    query.term_ids[2] = 30;
    
    UnifiedSearchResult results[10];
    int count = engine_search_async(engine, &query, results, 10);
    
    printf("Found %d results for exact phrase {10, 20, 30}\n", count);
    if (count > 0) {
        printf("Score: %f\n", results[0].bm25f_score);
    }
    
    printf("[4] Querying non-matching phrase {10, 30, 20}...\n");
    query.term_ids[0] = 10;
    query.term_ids[1] = 30;
    query.term_ids[2] = 20;
    count = engine_search_async(engine, &query, results, 10);
    printf("Found %d results for phrase {10, 30, 20}\n", count);

    async_search_shutdown();
    engine_close(engine);
    free(engine);
    return 0;
}
