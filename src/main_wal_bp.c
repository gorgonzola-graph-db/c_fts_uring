#include "wal.h"
#include "buffer_pool.h"
#include "shm_manager.h"
#include "slotted_page.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define DEMO_TABLE_FILE "/dev/shm/wal_bp_table.dat"
#define DEMO_WAL_FILE   "/dev/shm/wal_bp_journal.wal"
#define DEMO_SHM_NAME   "/c_db_buffer_shm"

static void recovery_apply_callback(const WALHeader *hdr, const char *payload) {
    printf("  [WAL Redo Replay] LSN: %lu | TxnID: %u | PageID: %u | SlotID: %u | Type: %u | PayloadLen: %u\n",
           hdr->lsn, hdr->txn_id, hdr->page_id, hdr->slot_id, hdr->record_type, hdr->payload_len);
}

int main() {
    printf("===================================================================\n");
    printf("Initializing WAL, Shared Memory & Buffer Pool Subsystem Benchmark\n");
    printf("===================================================================\n\n");

    // 1. Initialize WAL Manager
    WALManager wal;
    if (wal_init(&wal, DEMO_WAL_FILE) < 0) {
        return EXIT_FAILURE;
    }
    printf("[1/4] Write-Ahead Log (WAL) initialized at '%s'\n", DEMO_WAL_FILE);

    // 2. Initialize POSIX Shared Memory Buffer
    SharedMemoryBuffer shm;
    if (shm_manager_create(&shm, DEMO_SHM_NAME, PAGE_SIZE * 4) == 0) {
        printf("[2/4] POSIX Shared Memory created ('%s', Size: %zu bytes)\n", DEMO_SHM_NAME, shm.size);
    }

    // 3. Initialize Disk File & Buffer Pool Manager
    int disk_fd = open(DEMO_TABLE_FILE, O_CREAT | O_RDWR, 0644);
    BufferPoolManager bpm;
    buffer_pool_init(&bpm, disk_fd, &wal);
    printf("[3/4] Buffer Pool Manager initialized (Size: %d frames, Clock Eviction Policy)\n\n", BUFFER_POOL_SIZE);

    // 4. Transaction Workload Simulation (Fetching, Modifying & Evicting Pages)
    printf("--- Workload Execution & WAL Logging ---\n");
    uint32_t txn_id = 101;

    for (uint32_t page_id = 0; page_id < 10; page_id++) {
        // Fetch page into buffer pool (will trigger LRU eviction when page_id > 7)
        Frame *frame = buffer_pool_fetch_page(&bpm, page_id);
        if (!frame) continue;

        // Log mutation to WAL first (WAL Protocol)
        char mock_tuple[64];
        snprintf(mock_tuple, sizeof(mock_tuple), "Row Data for Page %u (Txn %u)", page_id, txn_id);

        lsn_t lsn = wal_append_record(&wal, txn_id, WAL_REC_INSERT, page_id, 0, mock_tuple, strlen(mock_tuple) + 1);
        frame->page_lsn = lsn;

        // Modify page in buffer frame
        slotted_page_insert_tuple(frame->page_data, mock_tuple, strlen(mock_tuple) + 1);

        // Unpin frame and mark dirty
        buffer_pool_unpin_page(&bpm, page_id, true, HINT_NORMAL);
        printf("  [Txn %u] Modified Page %u (LSN %lu, Frame Pinned: %u)\n", txn_id, page_id, lsn, frame->pin_count);
    }

    // Append Commit Log Record
    wal_append_record(&wal, txn_id, WAL_REC_COMMIT, 0, 0, NULL, 0);
    wal_flush(&wal);

    // Flush dirty buffer frames to disk
    printf("\nFlushing uncheckpointed dirty frames from Buffer Pool to disk...\n");
    buffer_pool_flush_all(&bpm);

    // 5. Crash Recovery Simulation (Replaying WAL Log)
    printf("\n[4/4] --- Simulating Crash Recovery / WAL Redo Log Replay ---\n");
    int replayed_records = wal_replay_recovery(&wal, recovery_apply_callback);
    printf("Crash Recovery Completed: %d WAL log records replayed.\n", replayed_records);

    // Cleanup resources
    wal_close(&wal);
    close(disk_fd);
    shm_manager_detach(&shm);
    shm_manager_unlink(DEMO_SHM_NAME);
    unlink(DEMO_TABLE_FILE);
    unlink(DEMO_WAL_FILE);

    printf("\n===================================================================\n");
    printf("WAL & Buffer Pool Subsystem Benchmark Execution Completed!\n");
    printf("===================================================================\n");

    return EXIT_SUCCESS;
}
