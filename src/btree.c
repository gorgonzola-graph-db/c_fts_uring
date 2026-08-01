#include "btree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void btree_init(BTree *tree, BufferPoolManager *bpm, uint32_t root_page_id, uint32_t *next_page_id_counter) {
    tree->bpm = bpm;
    tree->root_page_id = root_page_id;
    tree->next_page_id = next_page_id_counter;  // Store pointer, not value

    // Fetch root page to verify or initialize
    Frame *f = buffer_pool_fetch_page(tree->bpm, tree->root_page_id);
    if (f) {
        PageHeader *hdr = (PageHeader *)f->page_data;
        if (hdr->page_type != PAGE_TYPE_BTREE_LEAF && hdr->page_type != PAGE_TYPE_BTREE_INTERNAL) {
            slotted_page_init(f->page_data, tree->root_page_id, PAGE_TYPE_BTREE_LEAF);
            BTreeLeafNode *leaf = (BTreeLeafNode *)f->page_data;
            leaf->next_leaf_id = INVALID_PAGE_ID;
            slotted_page_compute_checksum(f->page_data);
            f->is_dirty = true;
        }
        buffer_pool_unpin_page(tree->bpm, tree->root_page_id, f->is_dirty, HINT_NORMAL);
    }
}

bool btree_search(BTree *tree, uint64_t key, uint32_t *out_page_id, uint16_t *out_slot_id) {
    uint32_t curr_page_id = tree->root_page_id;

    while (curr_page_id != INVALID_PAGE_ID) {
        Frame *f = buffer_pool_fetch_page(tree->bpm, curr_page_id);
        if (!f) return false;

        PageHeader *hdr = (PageHeader *)f->page_data;
        
        if (hdr->page_type == PAGE_TYPE_BTREE_INTERNAL) {
            BTreeInternalNode *node = (BTreeInternalNode *)f->page_data;
            uint32_t next_page = node->first_child_id;
            
            for (uint16_t i = 0; i < hdr->slot_count; i++) {
                if (key >= node->entries[i].key) {
                    next_page = node->entries[i].child_page_id;
                } else {
                    break;
                }
            }
            buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
            curr_page_id = next_page;
        } else if (hdr->page_type == PAGE_TYPE_BTREE_LEAF) {
            BTreeLeafNode *leaf = (BTreeLeafNode *)f->page_data;
            bool found = false;
            
            for (uint16_t i = 0; i < hdr->slot_count; i++) {
                if (leaf->entries[i].key == key) {
                    if (out_page_id) *out_page_id = leaf->entries[i].page_id;
                    if (out_slot_id) *out_slot_id = leaf->entries[i].slot_id;
                    found = true;
                    break;
                }
            }
            uint32_t next_leaf = leaf->next_leaf_id;
            buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
            if (found) return true;
            curr_page_id = next_leaf; // Follow linked leaf list if key moved during split
        } else {
            buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
            return false;
        }
    }
    return false;
}

static void split_leaf_node(BTree *tree, uint32_t leaf_page_id, uint32_t txn_id) {
    Frame *f_old = buffer_pool_fetch_page(tree->bpm, leaf_page_id);
    if (!f_old) return;

    BTreeLeafNode *old_leaf = (BTreeLeafNode *)f_old->page_data;
    uint32_t new_leaf_id = (*tree->next_page_id)++;

    // Allocate disk space for the new page
    char zero[4096] = {0};
    lseek(tree->bpm->disk_fd, new_leaf_id * 4096, SEEK_SET);
    write(tree->bpm->disk_fd, zero, 4096);

    Frame *f_new = buffer_pool_fetch_page(tree->bpm, new_leaf_id);
    if (!f_new) {
        buffer_pool_unpin_page(tree->bpm, leaf_page_id, false, HINT_NORMAL);
        return;
    }

    slotted_page_init(f_new->page_data, new_leaf_id, PAGE_TYPE_BTREE_LEAF);
    BTreeLeafNode *new_leaf = (BTreeLeafNode *)f_new->page_data;

    uint16_t mid = old_leaf->header.slot_count / 2;
    uint16_t move_count = old_leaf->header.slot_count - mid;

    // Move half the entries to new_leaf
    memcpy(new_leaf->entries, &old_leaf->entries[mid], move_count * sizeof(BTreeLeafEntry));
    new_leaf->header.slot_count = move_count;
    old_leaf->header.slot_count = mid;

    // Link leaves
    new_leaf->next_leaf_id = old_leaf->next_leaf_id;
    old_leaf->next_leaf_id = new_leaf_id;

    // Log WAL record if WAL manager exists
    if (tree->bpm->wal) {
        uint64_t split_info[2] = { leaf_page_id, new_leaf_id };
        lsn_t lsn = wal_append_record(tree->bpm->wal, txn_id, WAL_BTREE_SPLIT, leaf_page_id, 0, split_info, sizeof(split_info));
        old_leaf->header.lsn = lsn;
        new_leaf->header.lsn = lsn;
    }

    slotted_page_compute_checksum(f_old->page_data);
    slotted_page_compute_checksum(f_new->page_data);

    buffer_pool_unpin_page(tree->bpm, leaf_page_id, true, HINT_NORMAL);
    buffer_pool_unpin_page(tree->bpm, new_leaf_id, true, HINT_NORMAL);
}

bool btree_insert(BTree *tree, uint64_t key, uint32_t page_id, uint16_t slot_id, uint32_t txn_id) {
    uint32_t curr_page_id = tree->root_page_id;

    Frame *f = buffer_pool_fetch_page(tree->bpm, curr_page_id);
    if (!f) return false;

    PageHeader *hdr = (PageHeader *)f->page_data;
    if (hdr->page_type != PAGE_TYPE_BTREE_LEAF) {
        // Simple prototype leaf insert (handling internal node routing in production split)
        buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
        return false;
    }

    BTreeLeafNode *leaf = (BTreeLeafNode *)f->page_data;

    // Check if leaf needs split
    if (leaf->header.slot_count >= BTREE_MAX_LEAF_ENTRIES) {
        buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
        split_leaf_node(tree, curr_page_id, txn_id);
        // Re-fetch after split
        f = buffer_pool_fetch_page(tree->bpm, curr_page_id);
        leaf = (BTreeLeafNode *)f->page_data;
    }

    // Traverse to correct leaf segment if key belongs to a split right-sibling
    while (leaf->next_leaf_id != INVALID_PAGE_ID && leaf->header.slot_count > 0 && key > leaf->entries[leaf->header.slot_count - 1].key) {
        uint32_t next_id = leaf->next_leaf_id;
        buffer_pool_unpin_page(tree->bpm, curr_page_id, false, HINT_NORMAL);
        curr_page_id = next_id;
        f = buffer_pool_fetch_page(tree->bpm, curr_page_id);
        leaf = (BTreeLeafNode *)f->page_data;
    }

    // Insert sorted
    int insert_idx = leaf->header.slot_count;
    for (int i = 0; i < leaf->header.slot_count; i++) {
        if (leaf->entries[i].key > key) {
            insert_idx = i;
            break;
        }
    }

    // Shift entries right
    for (int i = leaf->header.slot_count; i > insert_idx; i--) {
        leaf->entries[i] = leaf->entries[i - 1];
    }

    leaf->entries[insert_idx].key = key;
    leaf->entries[insert_idx].page_id = page_id;
    leaf->entries[insert_idx].slot_id = slot_id;
    leaf->header.slot_count++;

    // Write-Ahead Logging for BTree Insert
    if (tree->bpm->wal) {
        lsn_t lsn = wal_append_record(tree->bpm->wal, txn_id, WAL_REC_INSERT, curr_page_id, (uint16_t)insert_idx, &key, sizeof(key));
        leaf->header.lsn = lsn;
    }

    slotted_page_compute_checksum(f->page_data);
    buffer_pool_unpin_page(tree->bpm, curr_page_id, true, HINT_NORMAL);
    return true;
}
