#include "shm_manager.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int shm_manager_create(SharedMemoryBuffer *shm, const char *name, size_t size) {
    memset(shm, 0, sizeof(SharedMemoryBuffer));
    strncpy(shm->shm_name, name, sizeof(shm->shm_name) - 1);
    shm->size = size;

    shm->shm_fd = shm_open(name, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (shm->shm_fd < 0) {
        perror("shm_open create failed");
        return -1;
    }

    if (ftruncate(shm->shm_fd, size) < 0) {
        perror("ftruncate SHM failed");
        close(shm->shm_fd);
        return -1;
    }

    shm->mapped_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->shm_fd, 0);
    if (shm->mapped_addr == MAP_FAILED) {
        perror("mmap SHM failed");
        close(shm->shm_fd);
        return -1;
    }

    return 0;
}

int shm_manager_attach(SharedMemoryBuffer *shm, const char *name, size_t size) {
    memset(shm, 0, sizeof(SharedMemoryBuffer));
    strncpy(shm->shm_name, name, sizeof(shm->shm_name) - 1);
    shm->size = size;

    shm->shm_fd = shm_open(name, O_RDWR, 0666);
    if (shm->shm_fd < 0) {
        perror("shm_open attach failed");
        return -1;
    }

    shm->mapped_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm->shm_fd, 0);
    if (shm->mapped_addr == MAP_FAILED) {
        perror("mmap attach SHM failed");
        close(shm->shm_fd);
        return -1;
    }

    return 0;
}

void shm_manager_detach(SharedMemoryBuffer *shm) {
    if (shm->mapped_addr && shm->mapped_addr != MAP_FAILED) {
        munmap(shm->mapped_addr, shm->size);
        shm->mapped_addr = NULL;
    }
    if (shm->shm_fd >= 0) {
        close(shm->shm_fd);
        shm->shm_fd = -1;
    }
}

void shm_manager_unlink(const char *name) {
    shm_unlink(name);
}
