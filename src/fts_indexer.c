#include "fts_indexer.h"
#include <string.h>
#include <math.h>

uint8_t quantize_field_length(uint32_t len) {
    if (len == 0) return 0;
    // Logarithmic quantization from uint32_t space to uint8_t [1..255]
    double log_val = log2((double)len + 1.0) * 16.0;
    if (log_val > 255.0) return 255;
    return (uint8_t)log_val;
}

void fts_length_map_init(char *page_buf, uint32_t page_id) {
    slotted_page_init(page_buf, page_id, PAGE_TYPE_LENGTH_MAP);
}

void fts_length_map_set(char *page_buf, uint32_t doc_id, uint8_t field_idx, uint8_t quantized_len) {
    if (field_idx >= MAX_FTS_FIELDS) return;
    uint32_t idx = doc_id % DOC_NORMS_PER_PAGE;
    size_t offset = sizeof(PageHeader) + (idx * sizeof(DocNormEntry)) + field_idx;
    if (offset < PAGE_SIZE) {
        page_buf[offset] = (char)quantized_len;
    }
}

uint8_t fts_length_map_get(const char *page_buf, uint32_t doc_id, uint8_t field_idx) {
    if (field_idx >= MAX_FTS_FIELDS) return 0;
    uint32_t idx = doc_id % DOC_NORMS_PER_PAGE;
    size_t offset = sizeof(PageHeader) + (idx * sizeof(DocNormEntry)) + field_idx;
    if (offset < PAGE_SIZE) {
        return (uint8_t)page_buf[offset];
    }
    return 0;
}

void fts_tombstone_init(char *page_buf, uint32_t page_id) {
    slotted_page_init(page_buf, page_id, PAGE_TYPE_TOMBSTONE_BITSET);
}

void fts_tombstone_mark_deleted(char *page_buf, uint32_t doc_id) {
    uint32_t bit_offset = doc_id % TOMBSTONE_BITS_PER_PAGE;
    size_t byte_idx = sizeof(PageHeader) + (bit_offset / 8);
    uint8_t bit_idx = bit_offset % 8;
    if (byte_idx < PAGE_SIZE) {
        page_buf[byte_idx] |= (1 << bit_idx);
    }
}

bool fts_tombstone_is_deleted(const char *page_buf, uint32_t doc_id) {
    uint32_t bit_offset = doc_id % TOMBSTONE_BITS_PER_PAGE;
    size_t byte_idx = sizeof(PageHeader) + (bit_offset / 8);
    uint8_t bit_idx = bit_offset % 8;
    if (byte_idx < PAGE_SIZE) {
        return (page_buf[byte_idx] & (1 << bit_idx)) != 0;
    }
    return false;
}
