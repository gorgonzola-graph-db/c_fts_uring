#include "wal.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int wal_init(WALManager *wal, const char *filepath) {
    memset(wal, 0, sizeof(WALManager));
    strncpy(wal->filepath, filepath, sizeof(wal->filepath) - 1);
    
    wal->wal_fd = open(filepath, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (wal->wal_fd < 0) {
        perror("Failed to open WAL file");
        return -1;
    }

    // Determine current LSN by seeking to end of file
    off_t size = lseek(wal->wal_fd, 0, SEEK_END);
    wal->current_lsn = (lsn_t)size;
    return 0;
}

lsn_t wal_append_record(WALManager *wal, uint32_t txn_id, WALRecordType rec_type, uint32_t page_id, uint16_t slot_id, const void *payload, uint32_t payload_len) {
    WALHeader hdr;
    hdr.lsn = wal->current_lsn + 1;
    hdr.txn_id = txn_id;
    hdr.record_type = (uint8_t)rec_type;
    hdr.page_id = page_id;
    hdr.slot_id = slot_id;
    hdr.payload_len = payload_len;

    // Write header
    if (write(wal->wal_fd, &hdr, sizeof(WALHeader)) != sizeof(WALHeader)) {
        perror("Failed to write WAL header");
        return 0;
    }

    // Write payload if present
    if (payload_len > 0 && payload != NULL) {
        if (write(wal->wal_fd, payload, payload_len) != (ssize_t)payload_len) {
            perror("Failed to write WAL payload");
            return 0;
        }
    }

    wal->current_lsn = hdr.lsn;
    return hdr.lsn;
}

int wal_flush(WALManager *wal) {
    if (wal->wal_fd >= 0) {
        return fdatasync(wal->wal_fd);
    }
    return -1;
}

int wal_replay_recovery(WALManager *wal, void (*apply_cb)(const WALHeader *hdr, const char *payload)) {
    if (wal->wal_fd < 0) return -1;

    lseek(wal->wal_fd, 0, SEEK_SET);
    int replayed = 0;

    while (1) {
        WALHeader hdr;
        ssize_t n = read(wal->wal_fd, &hdr, sizeof(WALHeader));
        if (n <= 0) break; // EOF or error

        char *payload_buf = NULL;
        if (hdr.payload_len > 0) {
            payload_buf = malloc(hdr.payload_len);
            if (read(wal->wal_fd, payload_buf, hdr.payload_len) != (ssize_t)hdr.payload_len) {
                free(payload_buf);
                break;
            }
        }

        if (apply_cb) {
            apply_cb(&hdr, payload_buf);
        }

        if (payload_buf) free(payload_buf);
        replayed++;
    }

    return replayed;
}

void wal_close(WALManager *wal) {
    if (wal->wal_fd >= 0) {
        wal_flush(wal);
        close(wal->wal_fd);
        wal->wal_fd = -1;
    }
}
