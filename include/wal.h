#ifndef WAL_H
#define WAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t lsn_t;

typedef enum {
    WAL_REC_INSERT = 1,
    WAL_REC_UPDATE = 2,
    WAL_REC_COMMIT = 3,
    WAL_REC_ABORT  = 4,
    WAL_BTREE_SPLIT = 5,
    WAL_FTS_TOMBSTONE = 6,
    WAL_FTS_DELTA_INSERT = 7
} WALRecordType;

#pragma pack(push, 1)
typedef struct {
    lsn_t lsn;
    uint32_t txn_id;
    uint8_t record_type; // WALRecordType
    uint32_t page_id;
    uint16_t slot_id;
    uint32_t payload_len;
} WALHeader;
#pragma pack(pop)

typedef struct {
    WALHeader header;
    char *payload;
} WALRecord;

typedef struct {
    int wal_fd;
    char filepath[256];
    lsn_t current_lsn;
} WALManager;

// Function prototypes
int wal_init(WALManager *wal, const char *filepath);
lsn_t wal_append_record(WALManager *wal, uint32_t txn_id, WALRecordType rec_type, uint32_t page_id, uint16_t slot_id, const void *payload, uint32_t payload_len);
int wal_flush(WALManager *wal);
int wal_replay_recovery(WALManager *wal, void (*apply_cb)(const WALHeader *hdr, const char *payload));
void wal_close(WALManager *wal);

#endif // WAL_H
