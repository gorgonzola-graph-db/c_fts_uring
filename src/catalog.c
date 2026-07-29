#include "catalog.h"
#include <string.h>
#include <stdio.h>

void catalog_init_table(Catalog *cat, const char *table_name) {
    memset(cat, 0, sizeof(Catalog));
    strncpy(cat->schema.table_name, table_name, MAX_NAME_LEN - 1);
    cat->schema.column_count = 0;
    cat->schema.tuple_size = 0;
    cat->shard_count = 0;
}

void catalog_add_column(Catalog *cat, const char *col_name, DataType type, uint32_t length) {
    if (cat->schema.column_count >= MAX_COLUMNS) return;
    
    ColumnDescriptor *col = &cat->schema.columns[cat->schema.column_count];
    strncpy(col->name, col_name, MAX_NAME_LEN - 1);
    col->type = type;
    col->offset = cat->schema.tuple_size;
    col->length = length;

    cat->schema.tuple_size += length;
    cat->schema.column_count++;
}

void catalog_add_shard(Catalog *cat, uint32_t shard_id, const char *filepath, int32_t min_k, int32_t max_k, uint64_t rows) {
    if (cat->shard_count >= MAX_SHARDS) return;
    
    ShardMapEntry *shard = &cat->shards[cat->shard_count];
    shard->shard_id = shard_id;
    strncpy(shard->filepath, filepath, sizeof(shard->filepath) - 1);
    shard->min_key = min_k;
    shard->max_key = max_k;
    shard->row_count = rows;
    
    cat->shard_count++;
}

// Shard Pruning: Evaluates whether a shard's min/max key range intersects the query predicate range
bool catalog_should_scan_shard(const ShardMapEntry *shard, int32_t query_min_key, int32_t query_max_key) {
    // If shard's max_key is less than query min_key OR shard's min_key is greater than query max_key, prune it
    if (shard->max_key < query_min_key || shard->min_key > query_max_key) {
        return false; // Pruned: No I/O dispatched to this shard
    }
    return true; // Keep: Shard contains relevant row ranges
}
