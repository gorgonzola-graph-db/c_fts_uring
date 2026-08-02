#ifndef FTS_VFS_H
#define FTS_VFS_H

#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __linux__
#include <liburing.h>
#define FTS_USE_URING
#else
// Fallback backend
#endif

typedef struct fts_vfs_context {
#ifdef FTS_USE_URING
    struct io_uring ring;
    int submitted_count;
#else
    #define MAX_BATCH_EVENTS 1024
    void* batch_user_data[MAX_BATCH_EVENTS];
    int batch_results[MAX_BATCH_EVENTS];
    int batch_count;
#endif
    bool initialized;
} fts_vfs_context_t;

// Initialize the VFS context (e.g., io_uring_queue_init)
int fts_vfs_init(fts_vfs_context_t *ctx, uint32_t queue_size);

// Destroy the VFS context
void fts_vfs_destroy(fts_vfs_context_t *ctx);

// Synchronous read
ssize_t fts_vfs_read_sync(fts_vfs_context_t *ctx, int fd, void *buf, size_t count, off_t offset);

// Synchronous write
ssize_t fts_vfs_write_sync(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset);

// Batch write functions
void fts_vfs_batch_begin(fts_vfs_context_t *ctx);
int fts_vfs_batch_add_write(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset, void *user_data);
int fts_vfs_batch_submit_and_wait(fts_vfs_context_t *ctx, void **completed_user_data, int max_events);

#endif // FTS_VFS_H
