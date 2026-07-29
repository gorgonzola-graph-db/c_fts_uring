#include "buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void buffer_pool_init(BufferPoolManager *bpm, int disk_fd, WALManager *wal) {
    memset(bpm, 0, sizeof(BufferPoolManager));
    bpm->disk_fd = disk_fd;
    bpm->wal = wal;
    bpm->clock_hand = 0;

    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        bpm->frames[i].page_id = (uint32_t)-1; // Invalid page_id
    }
}

int buffer_pool_flush_page(BufferPoolManager *bpm, uint32_t page_id) {
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        Frame *f = &bpm->frames[i];
        if (f->page_id == page_id) {
            if (f->is_dirty && bpm->disk_fd >= 0) {
                // WAL protocol: Ensure WAL is flushed up to page LSN before writing dirty page to disk
                if (bpm->wal) {
                    wal_flush(bpm->wal);
                }
                
                off_t offset = (off_t)page_id * PAGE_SIZE;
                lseek(bpm->disk_fd, offset, SEEK_SET);
                write(bpm->disk_fd, f->page_data, PAGE_SIZE);
                f->is_dirty = false;
                printf("  [BufferPool] Dirty Page %u flushed to disk (Offset %ld).\n", page_id, offset);
            }
            return 0;
        }
    }
    return -1;
}

static int find_victim_frame(BufferPoolManager *bpm) {
    for (int attempts = 0; attempts < BUFFER_POOL_SIZE * 2; attempts++) {
        Frame *f = &bpm->frames[bpm->clock_hand];
        uint32_t current_idx = bpm->clock_hand;
        bpm->clock_hand = (bpm->clock_hand + 1) % BUFFER_POOL_SIZE;

        if (f->page_id == (uint32_t)-1 || f->pin_count == 0) {
            if (f->is_dirty) {
                buffer_pool_flush_page(bpm, f->page_id);
            }
            return current_idx;
        }
    }
    return -1; // All frames pinned
}

Frame* buffer_pool_fetch_page(BufferPoolManager *bpm, uint32_t page_id) {
    // 1. Check if page is already cached in pool
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bpm->frames[i].page_id == page_id) {
            bpm->frames[i].pin_count++;
            return &bpm->frames[i];
        }
    }

    // 2. Select victim frame via LRU Clock Eviction
    int frame_idx = find_victim_frame(bpm);
    if (frame_idx < 0) {
        fprintf(stderr, "[BufferPool Error] All buffer frames are currently pinned!\n");
        return NULL;
    }

    Frame *f = &bpm->frames[frame_idx];
    f->page_id = page_id;
    f->pin_count = 1;
    f->is_dirty = false;
    f->page_lsn = 0;

    // Read page from disk into frame
    if (bpm->disk_fd >= 0) {
        off_t offset = (off_t)page_id * PAGE_SIZE;
        lseek(bpm->disk_fd, offset, SEEK_SET);
        ssize_t bytes_read = read(bpm->disk_fd, f->page_data, PAGE_SIZE);
        if (bytes_read < PAGE_SIZE) {
            slotted_page_init(f->page_data, page_id, PAGE_TYPE_ROW);
        }
    } else {
        slotted_page_init(f->page_data, page_id, PAGE_TYPE_ROW);
    }

    return f;
}

void buffer_pool_unpin_page(BufferPoolManager *bpm, uint32_t page_id, bool is_dirty, BufferPoolHint hint) {
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bpm->frames[i].page_id == page_id) {
            if (bpm->frames[i].pin_count > 0) {
                bpm->frames[i].pin_count--;
            }
            if (is_dirty) {
                bpm->frames[i].is_dirty = true;
            }
            
            // MRU override for sequential scans: evict this frame next
            if (bpm->frames[i].pin_count == 0 && hint == HINT_SEQUENTIAL_SCAN) {
                bpm->clock_hand = i;
            }
            return;
        }
    }
}

void buffer_pool_flush_all(BufferPoolManager *bpm) {
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bpm->frames[i].page_id != (uint32_t)-1 && bpm->frames[i].is_dirty) {
            buffer_pool_flush_page(bpm, bpm->frames[i].page_id);
        }
    }
}
