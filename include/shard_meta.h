#ifndef SHARD_META_H
#define SHARD_META_H

#include "unified_shard.h"

#define SHARD_META_MAGIC 0x46545321  // 'FTS!'
#define SHARD_META_VERSION 1

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t shard_id;
    uint32_t total_docs;
    uint32_t next_doc_id;
    uint32_t next_page_id;
    double   avgdl[4];
    double   sum_dl[4];
    uint8_t  reserved[64];
} ShardMetadata;
#pragma pack(pop)

int shard_meta_save(UnifiedShard *shard, const char *base_path);
int shard_meta_load(UnifiedShard *shard, const char *base_path);

#endif // SHARD_META_H
