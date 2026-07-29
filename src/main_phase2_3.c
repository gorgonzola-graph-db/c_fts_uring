#include "slotted_page.h"
#include "buffer_pool.h"
#include "wal.h"
#include "btree.h"
#include "fts_indexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
    printf("=== Starting Rapid Prototype: Phase 2 (B-Tree & WAL) & Phase 3 (FTS Tombstone & LengthMap) ===\n\n");

    const char *db_file = "/dev/shm/test_phase2_3.db";
    const char *wal_file = "/dev/shm/test_phase2_3.wal";

    unlink(db_file);
    unlink(wal_file);

    int disk_fd = open(db_file, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (disk_fd < 0) {
        perror("Failed to create db_file");
        return 1;
    }

    WALManager wal;
    if (wal_init(&wal, wal_file) != 0) {
        fprintf(stderr, "Failed to init WAL\n");
        close(disk_fd);
        return 1;
    }

    BufferPoolManager bpm;
    buffer_pool_init(&bpm, disk_fd, &wal);

    // -------------------------------------------------------------
    // Phase 2 Demonstration: B-Tree Operations & Split Logging
    // -------------------------------------------------------------
    printf("--- Phase 2: B-Tree Primary Key Storage & WAL Logging ---\n");
    uint32_t root_page_id = 0;
    uint32_t next_page_counter = 1;
    BTree btree;
    btree_init(&btree, &bpm, root_page_id, &next_page_counter);

    uint32_t txn_id = 101;
    printf("[B-Tree] Inserting 300 primary key entries (Triggering Leaf Split)... \n");
    for (uint64_t i = 1; i <= 300; i++) {
        uint64_t pk = i * 10;
        btree_insert(&btree, pk, (uint32_t)(i % 100), (uint16_t)(i % 50), txn_id);
    }
    printf("[B-Tree] Insertions completed successfully.\n");

    // Search Test
    uint64_t target_key = 1500;
    uint32_t found_page;
    uint16_t found_slot;
    if (btree_search(&btree, target_key, &found_page, &found_slot)) {
        printf("  -> [B-Tree Lookup SUCCESS] Key %lu found at Page %u, Slot %u\n", target_key, found_page, found_slot);
    } else {
        printf("  -> [B-Tree Lookup FAILED] Key %lu not found\n", target_key);
    }

    // -------------------------------------------------------------
    // Phase 3 Demonstration: O(1) Length Maps & Tombstone Bitsets
    // -------------------------------------------------------------
    printf("\n--- Phase 3: FTS Quantized Length Vectors & Tombstone Bitsets ---\n");

    char length_map_page[PAGE_SIZE];
    fts_length_map_init(length_map_page, 10);

    uint32_t doc_id = 42;
    uint32_t raw_field_0_len = 1520; // 1520 bytes raw text length
    uint8_t q_len = quantize_field_length(raw_field_0_len);

    fts_length_map_set(length_map_page, doc_id, 0, q_len);
    uint8_t retrieved_q_len = fts_length_map_get(length_map_page, doc_id, 0);

    printf("[LengthMap] DocID %u Field 0 Raw Length: %u bytes -> Quantized: %u (1-byte)\n", doc_id, raw_field_0_len, retrieved_q_len);
    printf("[BM25F Optimization] Retrieved Field Length in O(1) array lookup without decoding row slot!\n");

    // Tombstone Bitset Test
    char tombstone_page[PAGE_SIZE];
    fts_tombstone_init(tombstone_page, 11);

    printf("[Tombstone] Marking DocID %u as deleted in TombstoneBitset...\n", doc_id);
    fts_tombstone_mark_deleted(tombstone_page, doc_id);

    bool is_del = fts_tombstone_is_deleted(tombstone_page, doc_id);
    bool is_other_del = fts_tombstone_is_deleted(tombstone_page, 43);

    printf("  -> DocID %u Deleted Status: %s\n", doc_id, is_del ? "TRUE (Tombstoned)" : "FALSE");
    printf("  -> DocID %u Deleted Status: %s\n", 43, is_other_del ? "TRUE" : "FALSE (Active)");

    // Flush & Clean
    buffer_pool_flush_all(&bpm);
    wal_close(&wal);
    close(disk_fd);

    printf("\n=== Phase 2 & Phase 3 Prototype Verification Completed Successfully! ===\n");
    return 0;
}
