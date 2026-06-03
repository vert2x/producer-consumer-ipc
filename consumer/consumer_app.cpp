#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include "../common/parse_args.h"
#include "../common/runtime_control.h"
#include "../common/shm_init.h"

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [--log-interval-ms <ms>] [--shm-name <name>]\n", prog);
    fprintf(stderr, "Example: %s --log-interval-ms 1000 --shm-name /shm_ipc_demo\n", prog);
}

int main(int argc, char* argv[]) {
    const char* shm_name = DEFAULT_SHM_NAME;
    uint32_t log_interval_ms = 1000;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log-interval-ms") == 0 && i + 1 < argc) {
            if (!require_u32_arg("--log-interval-ms", argv[i + 1], log_interval_ms)) {
                return 1;
            }
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

    RuntimeControl control("consumer");

    uint64_t total_packets = 0;
    uint64_t total_bytes = 0;
    uint64_t interval_packets = 0;
    uint64_t interval_bytes = 0;
    uint64_t last_packet_ts_ns = 0;
    uint64_t interval_started_ns = monotonic_time_ns();
    uint64_t next_log_ns = interval_started_ns + static_cast<uint64_t>(log_interval_ms) * 1000000ull;

    printf("[consumer] started, waiting for packets  shm_name=%s  log_interval_ms=%u\n", shm_name, log_interval_ms);
    control.print_controls();
    printf("\n");

    while (!control.stopped()) {
        control.poll_input();
        if (control.paused()) {
            control.idle_while_paused();
            continue;
        }

        // Wait until at least one packet becomes available
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        if (sem_timedwait(&shm->data_ready, &ts) != 0) {
            if (errno == ETIMEDOUT) {
                continue;
            }
            if (control.stopped()) {
                break;
            }
            perror("sem_timedwait");
            break;
        }

        uint32_t head = shm->head.load(std::memory_order_relaxed);
        // uint8_t slot = head & RING_MASK;
        uint8_t* slot = slot_ptr(shm, head & RING_MASK);

        // sem_wait on data_ready synchronizes with the producer's sem_post, so the
        // slot contents written before publication are visible here.
        PacketHeader* hdr = slot_header(slot);
        uint8_t* data = slot_data(slot);
        uint32_t payload_size = hdr->payload_size;
        uint32_t checksum = payload_checksum(data, payload_size);
        bool checksum_ok = (checksum == hdr->checksum);
        uint64_t now_ns = monotonic_time_ns();
        last_packet_ts_ns = hdr->timestamp_ns;

        ++total_packets;
        total_bytes += payload_size;
        ++interval_packets;
        interval_bytes += payload_size;

        if (!checksum_ok) {
            uint64_t latency_ns = now_ns - hdr->timestamp_ns;
            printf("[consumer] checksum error  seq=%-4lu  ts=%llu  latency_ns=%llu  expected=%08x  actual=%08x  size=%u  data=[",
                   hdr->sequence,
                   static_cast<unsigned long long>(hdr->timestamp_ns),
                   static_cast<unsigned long long>(latency_ns),
                   hdr->checksum,
                   checksum,
                   payload_size);
            print_payload_preview(data, payload_size);
            printf("]\n");
            fflush(stdout);
        }

        if (log_interval_ms == 0 || now_ns >= next_log_ns) {
            uint64_t elapsed_ns = now_ns - interval_started_ns;
            double elapsed_sec = elapsed_ns > 0 ? static_cast<double>(elapsed_ns) / 1000000000.0 : 0.0;
            uint64_t packets_per_sec = elapsed_sec > 0.0
                ? static_cast<uint64_t>(static_cast<double>(interval_packets) / elapsed_sec + 0.5)
                : 0;
            uint64_t bytes_per_sec = elapsed_sec > 0.0
                ? static_cast<uint64_t>(static_cast<double>(interval_bytes) / elapsed_sec + 0.5)
                : 0;

            printf("[consumer] stats  total_packets=%llu  last_packet_ts=%llu  total_bytes=%llu  packets=%llu  packets_per_sec=%llu  bytes=%llu  bytes_per_sec=%llu\n",
                   static_cast<unsigned long long>(total_packets),
                   static_cast<unsigned long long>(last_packet_ts_ns),
                   static_cast<unsigned long long>(total_bytes),
                   static_cast<unsigned long long>(interval_packets),
                   static_cast<unsigned long long>(packets_per_sec),
                   static_cast<unsigned long long>(interval_bytes),
                   static_cast<unsigned long long>(bytes_per_sec));
            fflush(stdout);

            interval_packets = 0;
            interval_bytes = 0;
            interval_started_ns = now_ns;
            next_log_ns = now_ns + static_cast<uint64_t>(log_interval_ms) * 1000000ull;
        }

        shm->head.store(head + 1, std::memory_order_release);
        sem_post(&shm->space_ready);
    }

    printf("[consumer] final  total_packets=%llu  last_packet_ts=%llu  total_bytes=%llu\n",
           static_cast<unsigned long long>(total_packets),
           static_cast<unsigned long long>(last_packet_ts_ns),
           static_cast<unsigned long long>(total_bytes));
    fflush(stdout);

    close_shm(shm);
    return 0;
}
