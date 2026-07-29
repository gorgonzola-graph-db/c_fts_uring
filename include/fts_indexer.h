#ifndef FTS_INDEXER_H
#define FTS_INDEXER_H

#include "slotted_page.h"
#include "buffer_pool.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_FTS_FIELDS 4
#define DOC_NORMS_PER_PAGE (PAGE_SIZE / MAX_FTS_FIELDS) // 1024 docs per page
#define TOMBSTONE_BITS_PER_PAGE (PAGE_SIZE * 8)          // 32,768 docs per page

#pragma pack(push, 1)
typedef struct {
    uint8_t field_lengths[MAX_FTS_FIELDS]; // Quantized 1-byte field lengths
} DocNormEntry;
#pragma pack(pop)

// Quantized doc length mapping function (quantizes length [0..65535] into uint8_t [0..255])
uint8_t quantize_field_length(uint32_t len);

// Length Map page operations
void fts_length_map_init(char *page_buf, uint32_t page_id);
void fts_length_map_set(char *page_buf, uint32_t doc_id, uint8_t field_idx, uint8_t quantized_len);
uint8_t fts_length_map_get(const char *page_buf, uint32_t doc_id, uint8_t field_idx);

// Tombstone Bitset operations
void fts_tombstone_init(char *page_buf, uint32_t page_id);
void fts_tombstone_mark_deleted(char *page_buf, uint32_t doc_id);
bool fts_tombstone_is_deleted(const char *page_buf, uint32_t doc_id);

#endif // FTS_INDEXER_H
