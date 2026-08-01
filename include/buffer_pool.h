#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include "slotted_page.h"
#include "wal.h"
#include <stdint.h>
#include <stdbool.h>

#define BUFFER_POOL_SIZE 93750

typedef struct {
    uint32_t page_id;
    uint32_t pin_count;
    bool is_dirty;
    lsn_t page_lsn;
    char page_data[PAGE_SIZE];
} Frame;

#include <liburing.h>

typedef struct {
    Frame frames[BUFFER_POOL_SIZE];
    uint32_t clock_hand;
    int disk_fd;
    WALManager *wal;
    struct io_uring ring;
} BufferPoolManager;

typedef enum {
    HINT_NORMAL = 0,
    HINT_SEQUENTIAL_SCAN = 1 // Use MRU behavior for eviction
} BufferPoolHint;

// Function prototypes
void buffer_pool_init(BufferPoolManager *bpm, int disk_fd, WALManager *wal);
Frame* buffer_pool_fetch_page(BufferPoolManager *bpm, uint32_t page_id);
void buffer_pool_unpin_page(BufferPoolManager *bpm, uint32_t page_id, bool is_dirty, BufferPoolHint hint);
int buffer_pool_flush_page(BufferPoolManager *bpm, uint32_t page_id);
void buffer_pool_destroy(BufferPoolManager *bpm);
void buffer_pool_flush_all(BufferPoolManager *bpm);

#endif // BUFFER_POOL_H
