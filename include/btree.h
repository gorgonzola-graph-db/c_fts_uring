#ifndef BTREE_H
#define BTREE_H

#include "slotted_page.h"
#include "buffer_pool.h"
#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE 4096
#define INVALID_PAGE_ID ((uint32_t)-1)

#define BTREE_MAX_LEAF_ENTRIES 250
#define BTREE_MAX_INTERNAL_ENTRIES 300

typedef struct {
    uint64_t key;
    uint32_t page_id;
    uint16_t slot_id;
} BTreeLeafEntry;

typedef struct {
    uint64_t key;
    uint32_t child_page_id;
} BTreeInternalEntry;

#pragma pack(push, 1)
typedef struct {
    PageHeader header;
    uint32_t next_leaf_id;
    BTreeLeafEntry entries[BTREE_MAX_LEAF_ENTRIES];
} BTreeLeafNode;

typedef struct {
    PageHeader header;
    uint32_t first_child_id;
    BTreeInternalEntry entries[BTREE_MAX_INTERNAL_ENTRIES];
} BTreeInternalNode;
#pragma pack(pop)

typedef struct {
    BufferPoolManager *bpm;
    uint32_t root_page_id;
    uint32_t next_page_id;
} BTree;

void btree_init(BTree *tree, BufferPoolManager *bpm, uint32_t root_page_id, uint32_t *next_page_id_counter);
bool btree_search(BTree *tree, uint64_t key, uint32_t *out_page_id, uint16_t *out_slot_id);
bool btree_insert(BTree *tree, uint64_t key, uint32_t page_id, uint16_t slot_id, uint32_t txn_id);

#endif // BTREE_H
