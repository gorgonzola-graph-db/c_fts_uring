#include "buffer_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void buffer_pool_init(BufferPoolManager *bpm, int disk_fd, WALManager *wal, uint32_t pool_size) {
    memset(bpm, 0, sizeof(BufferPoolManager));
    bpm->disk_fd = disk_fd;
    bpm->wal = wal;
    bpm->clock_hand = 0;
    bpm->pool_size = pool_size;
    
    bpm->frames = malloc(sizeof(Frame) * pool_size);
    if (!bpm->frames) {
        fprintf(stderr, "Failed to allocate buffer pool frames (size: %u)\n", pool_size);
        exit(1);
    }

    for (uint32_t i = 0; i < pool_size; i++) {
        bpm->frames[i].page_id = (uint32_t)-1; // Invalid page_id
    }
    
    // Initialize io_uring with a queue size of 32
    if (io_uring_queue_init(32, &bpm->ring, 0) < 0) {
        fprintf(stderr, "Failed to initialize io_uring in BufferPoolManager\n");
    }
}

void buffer_pool_destroy(BufferPoolManager *bpm) {
    if (bpm->frames) {
        free(bpm->frames);
        bpm->frames = NULL;
    }
    io_uring_queue_exit(&bpm->ring);
}

int buffer_pool_flush_page(BufferPoolManager *bpm, uint32_t page_id) {
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
        Frame *f = &bpm->frames[i];
        if (f->page_id == page_id) {
            if (f->is_dirty && bpm->disk_fd >= 0) {
                // WAL protocol: Ensure WAL is flushed up to page LSN before writing dirty page to disk
                if (bpm->wal) {
                    wal_flush(bpm->wal);
                }
                
                off_t offset = (off_t)page_id * PAGE_SIZE;
                
                // Use io_uring for synchronous write (submit and wait)
                struct io_uring_sqe *sqe = io_uring_get_sqe(&bpm->ring);
                if (!sqe) return -1;
                
                io_uring_prep_write(sqe, bpm->disk_fd, f->page_data, PAGE_SIZE, offset);
                io_uring_submit(&bpm->ring);
                
                struct io_uring_cqe *cqe;
                io_uring_wait_cqe(&bpm->ring, &cqe);
                
                if (cqe->res < 0) {
                    io_uring_cqe_seen(&bpm->ring, cqe);
                    return -1;
                }
                io_uring_cqe_seen(&bpm->ring, cqe);
                
                f->is_dirty = false;
            }
            return 0;
        }
    }
    return -1;
}

static int find_victim_frame(BufferPoolManager *bpm) {
    for (uint32_t attempts = 0; attempts < bpm->pool_size * 2; attempts++) {
        Frame *f = &bpm->frames[bpm->clock_hand];
        uint32_t current_idx = bpm->clock_hand;
        bpm->clock_hand = (bpm->clock_hand + 1) % bpm->pool_size;

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
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
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
        
        struct io_uring_sqe *sqe = io_uring_get_sqe(&bpm->ring);
        if (!sqe) {
            slotted_page_init(f->page_data, page_id, PAGE_TYPE_ROW);
            return f;
        }
        
        io_uring_prep_read(sqe, bpm->disk_fd, f->page_data, PAGE_SIZE, offset);
        io_uring_submit(&bpm->ring);
        
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&bpm->ring, &cqe);
        
        ssize_t bytes_read = cqe->res;
        io_uring_cqe_seen(&bpm->ring, cqe);
        
        if (bytes_read < PAGE_SIZE) {
            slotted_page_init(f->page_data, page_id, PAGE_TYPE_ROW);
        }
    } else {
        slotted_page_init(f->page_data, page_id, PAGE_TYPE_ROW);
    }

    return f;
}

void buffer_pool_unpin_page(BufferPoolManager *bpm, uint32_t page_id, bool is_dirty, BufferPoolHint hint) {
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
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
    int submitted = 0;
    
    // First, ensure WAL is flushed if we have any dirty pages
    bool any_dirty = false;
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
        if (bpm->frames[i].page_id != (uint32_t)-1 && bpm->frames[i].is_dirty) {
            any_dirty = true;
            break;
        }
    }
    
    if (any_dirty && bpm->wal) {
        wal_flush(bpm->wal);
    }

    // Submit all writes at once using io_uring
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
        Frame *f = &bpm->frames[i];
        if (f->page_id != (uint32_t)-1 && f->is_dirty && bpm->disk_fd >= 0) {
            off_t offset = (off_t)f->page_id * PAGE_SIZE;
            struct io_uring_sqe *sqe = io_uring_get_sqe(&bpm->ring);
            if (sqe) {
                io_uring_prep_write(sqe, bpm->disk_fd, f->page_data, PAGE_SIZE, offset);
                // Tag the request so we can identify it if needed
                io_uring_sqe_set_data(sqe, f);
                submitted++;
            }
        }
    }
    
    if (submitted > 0) {
        io_uring_submit(&bpm->ring);
        
        // Wait for all submitted writes to complete
        for (int i = 0; i < submitted; i++) {
            struct io_uring_cqe *cqe;
            io_uring_wait_cqe(&bpm->ring, &cqe);
            
            Frame *f = (Frame *)io_uring_cqe_get_data(cqe);
            if (f && cqe->res >= 0) {
                f->is_dirty = false;
            }
            io_uring_cqe_seen(&bpm->ring, cqe);
        }
    }
}
