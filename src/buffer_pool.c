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
    
    // Initialize VFS with a queue size of 32
    if (fts_vfs_init(&bpm->vfs_ctx, 32) < 0) {
        fprintf(stderr, "Failed to initialize VFS in BufferPoolManager\n");
    }
}

void buffer_pool_destroy(BufferPoolManager *bpm) {
    if (bpm->frames) {
        free(bpm->frames);
        bpm->frames = NULL;
    }
    fts_vfs_destroy(&bpm->vfs_ctx);
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
                
                // Use VFS for synchronous write
                ssize_t res = fts_vfs_write_sync(&bpm->vfs_ctx, bpm->disk_fd, f->page_data, PAGE_SIZE, offset);
                if (res < 0) {
                    return -1;
                }
                
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
        
        ssize_t bytes_read = fts_vfs_read_sync(&bpm->vfs_ctx, bpm->disk_fd, f->page_data, PAGE_SIZE, offset);
        
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

    // Submit all writes at once using VFS
    fts_vfs_batch_begin(&bpm->vfs_ctx);
    for (uint32_t i = 0; i < bpm->pool_size; i++) {
        Frame *f = &bpm->frames[i];
        if (f->page_id != (uint32_t)-1 && f->is_dirty && bpm->disk_fd >= 0) {
            off_t offset = (off_t)f->page_id * PAGE_SIZE;
            if (fts_vfs_batch_add_write(&bpm->vfs_ctx, bpm->disk_fd, f->page_data, PAGE_SIZE, offset, f) == 0) {
                submitted++;
            }
        }
    }
    
    if (submitted > 0) {
        void *completed_data[1024];
        int completed = fts_vfs_batch_submit_and_wait(&bpm->vfs_ctx, completed_data, 1024);
        
        for (int i = 0; i < completed; i++) {
            Frame *f = (Frame *)completed_data[i];
            if (f) {
                f->is_dirty = false;
            }
        }
    }
}
