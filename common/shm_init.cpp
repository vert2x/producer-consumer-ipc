#include "shm_init.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

SharedMemory* open_shm(uint32_t payload_size) {
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return nullptr; }

    flock(fd, LOCK_EX);

    struct stat st;
    fstat(fd, &st);

    bool need_init = (st.st_size == 0);

    if (need_init) {
        // The first process to start must know payload_size
        if (payload_size == 0) {
            fprintf(stderr, "[shm] error: SHM does not exist yet, payload_size required\n");
            flock(fd, LOCK_UN);
            close(fd);
            return nullptr;
        }
        ftruncate(fd, static_cast<off_t>(shm_size(payload_size)));
    }

    // At this point, the SHM size is already set (either by us or by the first process)
    // Map only the header first so we can read payload_size
    SharedMemory* header = static_cast<SharedMemory*>(
        mmap(nullptr, sizeof(SharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    if (header == MAP_FAILED) { perror("mmap header"); flock(fd, LOCK_UN); close(fd); return nullptr; }

    if (need_init || header->magic != SHM_MAGIC) {
        // Initialize — first remap to the full size
        munmap(header, sizeof(SharedMemory));

        SharedMemory* shm = static_cast<SharedMemory*>(
            mmap(nullptr, shm_size(payload_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

        if (shm == MAP_FAILED) { perror("mmap full"); flock(fd, LOCK_UN); close(fd); return nullptr; }

        uint32_t next_generation = 1;
        if (!need_init) {
            next_generation = header->generation + 1;
        }

        memset(shm, 0, shm_size(payload_size));
        shm->generation = next_generation;
        shm->payload_size = payload_size;
        shm->head.store(0);
        shm->tail.store(0);
        sem_init(&shm->data_ready,  /*pshared=*/1, 0);
        sem_init(&shm->space_ready, /*pshared=*/1, RING_CAPACITY);
        shm->magic = SHM_MAGIC;   // set last — readiness flag

        printf("[shm] initialized: generation=%u  capacity=%u  payload_size=%u bytes  total=%zu bytes\n",
               shm->generation, RING_CAPACITY, payload_size, shm_size(payload_size));

        flock(fd, LOCK_UN);
        close(fd);
        return shm;
    }

    // SHM is already initialized — read payload_size from the header
    uint32_t existing_payload_size = header->payload_size;
    munmap(header, sizeof(SharedMemory));

    // If the caller passed a non-zero size, verify that it matches
    if (payload_size != 0 && payload_size != existing_payload_size) {
        fprintf(stderr, "[shm] error: requested payload_size=%u but SHM has payload_size=%u\n",
                payload_size, existing_payload_size);
        flock(fd, LOCK_UN);
        close(fd);
        return nullptr;
    }

    // Map the full region using the correct payload_size
    SharedMemory* shm = static_cast<SharedMemory*>(
        mmap(nullptr, shm_size(existing_payload_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    if (shm == MAP_FAILED) { perror("mmap full"); flock(fd, LOCK_UN); close(fd); return nullptr; }

    printf("[shm] attached: generation=%u  capacity=%u  payload_size=%u bytes\n",
           shm->generation, RING_CAPACITY, existing_payload_size);

    flock(fd, LOCK_UN);
    close(fd);
    return shm;
}

void close_shm(SharedMemory* shm) {
    if (!shm) {
        return;
    }

    munmap(shm, shm_size(shm->payload_size));
}

bool unlink_shm() {
    if (shm_unlink(SHM_NAME) != 0) {
        perror("shm_unlink");
        return false;
    }

    return true;
}
