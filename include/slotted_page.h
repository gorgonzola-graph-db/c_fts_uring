#ifndef SLOTTED_PAGE_H
#define SLOTTED_PAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 4096

typedef enum {
    PAGE_TYPE_INVALID = 0,
    PAGE_TYPE_ROW = 1,
    PAGE_TYPE_FTS_POSTING = 2,
    PAGE_TYPE_LENGTH_MAP = 3,
    PAGE_TYPE_BTREE_LEAF = 4,
    PAGE_TYPE_BTREE_INTERNAL = 5,
    PAGE_TYPE_TOMBSTONE_BITSET = 6
} PageType;

// Pointer slot in the slotted page header
typedef struct {
    uint16_t offset; // Offset from start of page to raw tuple bytes
    uint16_t length; // Length of tuple record
} Slot;

// Standard 24-byte Header for all pages
typedef struct {
    uint64_t lsn;             // 8 bytes: Log Sequence Number for WAL recovery
    uint32_t page_id;         // 4 bytes: Unique Page ID
    uint32_t checksum;        // 4 bytes: CRC32 of the page content
    uint16_t page_type;       // 2 bytes: PageType enum
    uint16_t free_space_lower;// 2 bytes: Offset after slot array (grows upward)
    uint16_t free_space_upper;// 2 bytes: Offset before raw tuple data (grows downward)
    uint16_t slot_count;      // 2 bytes: Number of slots allocated
} PageHeader;

// Function prototypes
void slotted_page_init(char *page_buf, uint32_t page_id, PageType type);
void slotted_page_compute_checksum(char *page_buf);
bool slotted_page_verify_checksum(const char *page_buf);
bool slotted_page_insert_tuple(char *page_buf, const void *tuple_data, uint16_t tuple_len);
const char* slotted_page_get_tuple(const char *page_buf, uint16_t slot_idx, uint16_t *out_len);

#endif // SLOTTED_PAGE_H
