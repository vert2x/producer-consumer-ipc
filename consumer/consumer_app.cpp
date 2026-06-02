#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "../common/shm_init.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int) {
    g_stop = 1;
}

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [--log-interval-ms <ms>] [--shm-name <name>]\n", prog);
    fprintf(stderr, "Example: %s --log-interval-ms 1000 --shm-name /shm_ipc_demo\n", prog);
}

int main(int argc, char* argv[]) {
    const char* shm_name = DEFAULT_SHM_NAME;
    uint32_t log_interval_ms = 1000;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log-interval-ms") == 0 && i + 1 < argc) {
            log_interval_ms = static_cast<uint32_t>(atoi(argv[i + 1]));
            ++i;
        } else if (strcmp(argv[i], "--shm-name") == 0 && i + 1 < argc) {
            shm_name = argv[i + 1];
            ++i;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    SharedMemory* shm = open_shm(0, shm_name);
    if (!shm) return 1;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    uint32_t generation = shm->generation;

    uint64_t next_log_ns = 0;

    printf("[consumer] started, waiting for packets  shm_name=%s  log_interval_ms=%u\n\n", shm_name, log_interval_ms);

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
        uint32_t checksum = payload_checksum(data, payload_size);
        bool checksum_ok = (checksum == hdr->checksum);
        uint64_t latency_ns = monotonic_time_ns() - hdr->timestamp_ns;

        uint64_t now_ns = monotonic_time_ns();
        if (!checksum_ok || log_interval_ms == 0 || now_ns >= next_log_ns) {
            printf("[consumer] received seq=%-4lu  ts=%llu  latency_ns=%llu  csum=%08x (%s)  size=%u  data=[",
                   hdr->sequence,
                   static_cast<unsigned long long>(hdr->timestamp_ns),
                   static_cast<unsigned long long>(latency_ns),
                   hdr->checksum,
                   checksum_ok ? "ok" : "bad",
                   payload_size);
            print_payload_preview(data, payload_size);
            printf("]\n");
            fflush(stdout);
            next_log_ns = now_ns + static_cast<uint64_t>(log_interval_ms) * 1000000ull;
        }

        shm->head.store(head + 1, std::memory_order_release);
        sem_post(&shm->space_ready);
    }

    close_shm(shm);
    return 0;
}
