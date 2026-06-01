#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <unistd.h>
#include "../common/shm_init.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int) {
    g_stop = 1;
}

int main() {
    SharedMemory* shm = open_shm(0);
    if (!shm) return 1;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    uint32_t generation = shm->generation;

    printf("[consumer] started, waiting for packets\n\n");

    while (!g_stop) {
        // Wait until at least one packet becomes available
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        if (sem_timedwait(&shm->data_ready, &ts) != 0) {
            if (errno == ETIMEDOUT) {
                continue;
            }
            if (g_stop) {
                break;
            }
            perror("sem_timedwait");
            break;
        }

        if (shm->generation != generation) {
            fprintf(stderr, "[consumer] error: shared memory was recreated, please restart consumer\n");
            break;
        }

        uint32_t head = shm->head.load(std::memory_order_relaxed);
        // uint8_t slot = head & RING_MASK;
        uint8_t* slot = slot_ptr(shm, head & RING_MASK);

        // acquire: see everything the producer wrote before its release
        PacketHeader* hdr = slot_header(slot);
        uint8_t* data = slot_data(slot);
        uint32_t payload_size = hdr->payload_size;

        printf("[consumer] received seq=%-4lu  size=%u  data=[",
               hdr->sequence, payload_size);

        uint32_t preview = std::min<uint32_t>(payload_size, 3);
        for (uint32_t i = 0; i < preview; ++i) {
            printf("%s%u", (i == 0 ? "" : ", "), static_cast<unsigned>(data[i]));
        }
        if (payload_size > preview) {
            printf(" ...");
        }
        printf("]\n");
        fflush(stdout);

        shm->head.store(head + 1, std::memory_order_release);
        sem_post(&shm->space_ready);
    }

    close_shm(shm);
    return 0;
}
