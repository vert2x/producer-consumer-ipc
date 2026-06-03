#include "shm_init.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
class InitLock {
public:
    explicit InitLock(const char* shm_name) {
        char name[256] = {};
        std::snprintf(name, sizeof(name), "%s.init_lock", shm_name);
        lock_ = sem_open(name, O_CREAT, 0666, 1);
        if (lock_ == SEM_FAILED) {
            lock_ = nullptr;
            std::perror("sem_open init lock");
            return;
        }

        if (sem_wait(lock_) != 0) {
            std::perror("sem_wait init lock");
            sem_close(lock_);
            lock_ = nullptr;
        }
    }

    ~InitLock() {
        if (lock_ != nullptr) {
            sem_post(lock_);
            sem_close(lock_);
        }
    }

    bool locked() const {
        return lock_ != nullptr;
    }

private:
    sem_t* lock_ = nullptr;
};
}  // namespace

SharedMemory* open_shm(uint32_t payload_size, const char* shm_name) {
    InitLock init_lock(shm_name);
    if (!init_lock.locked()) {
        return nullptr;
    }

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return nullptr; }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        close(fd);
        return nullptr;
    }

    bool need_init = (st.st_size == 0);

    if (need_init) {
        // The first process to start must know payload_size
        if (payload_size == 0) {
            fprintf(stderr, "[shm] error: SHM does not exist yet, payload_size required\n");
            close(fd);
            return nullptr;
        }
        if (ftruncate(fd, static_cast<off_t>(shm_size(payload_size))) != 0) {
            perror("ftruncate");
            close(fd);
            return nullptr;
        }
    }

    // At this point, the SHM size is already set (either by us or by the first process)
    // Map only the header first so we can read payload_size
    SharedMemory* header = static_cast<SharedMemory*>(
        mmap(nullptr, sizeof(SharedMemory), PROT_READ, MAP_SHARED, fd, 0));

    if (header == MAP_FAILED) { perror("mmap header"); close(fd); return nullptr; }

    if (need_init || header->magic != SHM_MAGIC) {
        // Initialize — first remap to the full size
        munmap(header, sizeof(SharedMemory));

        SharedMemory* shm = static_cast<SharedMemory*>(
            mmap(nullptr, shm_size(payload_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

        if (shm == MAP_FAILED) { perror("mmap full"); close(fd); return nullptr; }

        memset(shm, 0, shm_size(payload_size));
        shm->payload_size = payload_size;
        shm->head.store(0);
        shm->tail.store(0);
        sem_init(&shm->data_ready,  /*pshared=*/1, 0);
        sem_init(&shm->space_ready, /*pshared=*/1, RING_CAPACITY);
        shm->magic = SHM_MAGIC;   // set last — readiness flag

        printf("[shm] initialized: capacity=%u  payload_size=%u bytes  total=%zu bytes\n",
               RING_CAPACITY, payload_size, shm_size(payload_size));

        close(fd);
        return shm;
    }

    // SHM is already initialized — read payload_size from the header
    uint32_t existing_payload_size = header->payload_size;
    munmap(header, sizeof(SharedMemory));

    // If the caller passed a non-zero size, verify that it matches
    if (payload_size != 0 && payload_size != existing_payload_size) {
        fprintf(stderr,
                "[shm] error: requested payload_size=%u but SHM has payload_size=%u; use --reset to recreate SHM with the new size\n",
                payload_size, existing_payload_size);
        close(fd);
        return nullptr;
    }

    // Map the full region using the correct payload_size
    SharedMemory* shm = static_cast<SharedMemory*>(
        mmap(nullptr, shm_size(existing_payload_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    if (shm == MAP_FAILED) { perror("mmap full"); close(fd); return nullptr; }

    printf("[shm] attached: capacity=%u  payload_size=%u bytes\n",
           RING_CAPACITY, existing_payload_size);

    close(fd);
    return shm;
}

void close_shm(SharedMemory* shm) {
    if (!shm) {
        return;
    }

    munmap(shm, shm_size(shm->payload_size));
}

bool unlink_shm(const char* shm_name) {
    if (shm_unlink(shm_name) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        perror("shm_unlink");
        return false;
    }

    char lock_name[256] = {};
    std::snprintf(lock_name, sizeof(lock_name), "%s.init_lock", shm_name);
    sem_unlink(lock_name);
    return true;
}
