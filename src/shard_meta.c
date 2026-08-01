#include "shard_meta.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int shard_meta_save(UnifiedShard *shard, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/shard_%u.meta", base_path, shard->shard_id);

    ShardMetadata meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = SHARD_META_MAGIC;
    meta.version = SHARD_META_VERSION;
    meta.shard_id = shard->shard_id;
    meta.total_docs = shard->total_docs;
    meta.next_doc_id = shard->next_doc_id;
    meta.next_page_id = shard->next_page_id;

    for (int i = 0; i < MAX_FTS_FIELDS && i < 4; i++) {
        meta.avgdl[i] = shard->avgdl[i];
        meta.sum_dl[i] = shard->sum_dl[i];
    }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }

    ssize_t written = write(fd, &meta, sizeof(meta));
    if (written != sizeof(meta)) {
        close(fd);
        return -1;
    }

    if (fdatasync(fd) < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int shard_meta_load(UnifiedShard *shard, const char *base_path) {
    char path[512];
    snprintf(path, sizeof(path), "%s/shard_%u.meta", base_path, shard->shard_id);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1; // File does not exist or cannot be opened, which implies a fresh shard
    }

    ShardMetadata meta;
    ssize_t bytes_read = read(fd, &meta, sizeof(meta));
    close(fd);

    if (bytes_read != sizeof(meta)) {
        return -1;
    }

    if (meta.magic != SHARD_META_MAGIC) {
        return -1;
    }

    // Populate shard fields from metadata
    shard->total_docs = meta.total_docs;
    shard->next_doc_id = meta.next_doc_id;
    shard->next_page_id = meta.next_page_id;

    for (int i = 0; i < MAX_FTS_FIELDS && i < 4; i++) {
        shard->avgdl[i] = meta.avgdl[i];
        shard->sum_dl[i] = meta.sum_dl[i];
    }

    return 0;
}
