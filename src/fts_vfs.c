#include "fts_vfs.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef FTS_USE_URING

int fts_vfs_init(fts_vfs_context_t *ctx, uint32_t queue_size) {
    if (io_uring_queue_init(queue_size, &ctx->ring, 0) < 0) {
        return -1;
    }
    ctx->initialized = true;
    return 0;
}

void fts_vfs_destroy(fts_vfs_context_t *ctx) {
    if (ctx->initialized) {
        io_uring_queue_exit(&ctx->ring);
        ctx->initialized = false;
    }
}

ssize_t fts_vfs_read_sync(fts_vfs_context_t *ctx, int fd, void *buf, size_t count, off_t offset) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return -1;
    
    io_uring_prep_read(sqe, fd, buf, count, offset);
    io_uring_submit(&ctx->ring);
    
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ctx->ring, &cqe);
    
    ssize_t res = cqe->res;
    io_uring_cqe_seen(&ctx->ring, cqe);
    return res;
}

ssize_t fts_vfs_write_sync(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return -1;
    
    io_uring_prep_write(sqe, fd, buf, count, offset);
    io_uring_submit(&ctx->ring);
    
    struct io_uring_cqe *cqe;
    io_uring_wait_cqe(&ctx->ring, &cqe);
    
    ssize_t res = cqe->res;
    io_uring_cqe_seen(&ctx->ring, cqe);
    return res;
}

void fts_vfs_batch_begin(fts_vfs_context_t *ctx) {
    ctx->submitted_count = 0;
}

int fts_vfs_batch_add_write(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset, void *user_data) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ctx->ring);
    if (!sqe) return -1;
    io_uring_prep_write(sqe, fd, buf, count, offset);
    io_uring_sqe_set_data(sqe, user_data);
    ctx->submitted_count++;
    return 0;
}

int fts_vfs_batch_submit_and_wait(fts_vfs_context_t *ctx, void **completed_user_data, int max_events) {
    if (ctx->submitted_count == 0) return 0;
    
    io_uring_submit(&ctx->ring);
    
    int completed = 0;
    for (int i = 0; i < ctx->submitted_count; i++) {
        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ctx->ring, &cqe);
        
        if (completed < max_events && cqe->res >= 0) {
            completed_user_data[completed++] = io_uring_cqe_get_data(cqe);
        }
        io_uring_cqe_seen(&ctx->ring, cqe);
    }
    return completed;
}

#else
// Fallback Backend using pread/pwrite

int fts_vfs_init(fts_vfs_context_t *ctx, uint32_t queue_size) {
    ctx->initialized = true;
    return 0;
}

void fts_vfs_destroy(fts_vfs_context_t *ctx) {
    ctx->initialized = false;
}

ssize_t fts_vfs_read_sync(fts_vfs_context_t *ctx, int fd, void *buf, size_t count, off_t offset) {
    return pread(fd, buf, count, offset);
}

ssize_t fts_vfs_write_sync(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset) {
    return pwrite(fd, buf, count, offset);
}

void fts_vfs_batch_begin(fts_vfs_context_t *ctx) {
    ctx->batch_count = 0;
}

int fts_vfs_batch_add_write(fts_vfs_context_t *ctx, int fd, const void *buf, size_t count, off_t offset, void *user_data) {
    ssize_t res = pwrite(fd, buf, count, offset);
    if (ctx->batch_count < MAX_BATCH_EVENTS) {
        ctx->batch_user_data[ctx->batch_count] = user_data;
        ctx->batch_results[ctx->batch_count] = (res >= 0) ? 0 : -1;
        ctx->batch_count++;
    }
    return (res >= 0) ? 0 : -1;
}

int fts_vfs_batch_submit_and_wait(fts_vfs_context_t *ctx, void **completed_user_data, int max_events) {
    int completed = 0;
    for (int i = 0; i < ctx->batch_count; i++) {
        if (ctx->batch_results[i] >= 0 && completed < max_events) {
            completed_user_data[completed++] = ctx->batch_user_data[i];
        }
    }
    return completed;
}

#endif
