#include "include/unified_shard.h"
#include "include/db_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    printf("[1] Initializing Unified Engine direct C-API...\n");
    UnifiedEngine *engine = malloc(sizeof(UnifiedEngine));
    if (engine_init(engine, "./fts_uring_data", 1) != 0) {
        fprintf(stderr, "Failed to init engine\n");
        return 1;
    }
    
    printf("[2] Simulating Pecorino inserting AST node 'DataCollector'...\n");
    uint8_t node_id[16] = {
        0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
        0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef
    };
    
    // Simulate term frequencies and doc lengths for fields: name, kind, summary, body
    uint32_t tf[MAX_FTS_FIELDS] = {2, 1, 5, 10}; 
    uint32_t dl[MAX_FTS_FIELDS] = {10, 10, 50, 100};
    
    // Simulate 384D float embedding (e.g. from all-MiniLM-L6-v2)
    float embedding[EMBEDDING_DIM] = {0.0};
    embedding[0] = 0.95f; 
    
    // Insert into Shard 0 directly lock-free
    if (shard_insert_document(&engine->shards[0], &engine->shards[0].wal, node_id, tf, dl, embedding) != 0) {
        fprintf(stderr, "Failed to insert document\n");
    }
    
    // Flush to disk
    engine_close(engine);
    free(engine);
    printf("[3] Data flushed to ./fts_uring_data\n\n");
}
