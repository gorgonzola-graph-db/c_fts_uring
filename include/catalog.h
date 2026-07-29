#ifndef CATALOG_H
#define CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_NAME_LEN 64
#define MAX_COLUMNS 16
#define MAX_SHARDS 16

typedef enum {
    TYPE_INT32,
    TYPE_FLOAT64,
    TYPE_VARCHAR
} DataType;

typedef struct {
    char name[MAX_NAME_LEN];
    DataType type;
    uint32_t offset;
    uint32_t length;
} ColumnDescriptor;

typedef struct {
    char table_name[MAX_NAME_LEN];
    uint32_t column_count;
    ColumnDescriptor columns[MAX_COLUMNS];
    uint32_t tuple_size;
} TableSchema;

// Per-shard metadata statistics used by Query Planner for Shard Pruning
typedef struct {
    uint32_t shard_id;
    char filepath[256];
    int32_t min_key;    // Minimum indexed key value in shard
    int32_t max_key;    // Maximum indexed key value in shard
    uint64_t row_count; // Total active rows in shard
} ShardMapEntry;

typedef struct {
    TableSchema schema;
    uint32_t shard_count;
    ShardMapEntry shards[MAX_SHARDS];
} Catalog;

// Function prototypes
void catalog_init_table(Catalog *cat, const char *table_name);
void catalog_add_column(Catalog *cat, const char *col_name, DataType type, uint32_t length);
void catalog_add_shard(Catalog *cat, uint32_t shard_id, const char *filepath, int32_t min_k, int32_t max_k, uint64_t rows);
bool catalog_should_scan_shard(const ShardMapEntry *shard, int32_t query_min_key, int32_t query_max_key);

#endif // CATALOG_H
