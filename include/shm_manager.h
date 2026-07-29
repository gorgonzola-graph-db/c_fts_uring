#ifndef SHM_MANAGER_H
#define SHM_MANAGER_H

#include <stddef.h>

typedef struct {
    char shm_name[64];
    int shm_fd;
    size_t size;
    void *mapped_addr;
} SharedMemoryBuffer;

int shm_manager_create(SharedMemoryBuffer *shm, const char *name, size_t size);
int shm_manager_attach(SharedMemoryBuffer *shm, const char *name, size_t size);
void shm_manager_detach(SharedMemoryBuffer *shm);
void shm_manager_unlink(const char *name);

#endif // SHM_MANAGER_H
