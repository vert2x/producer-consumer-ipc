#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unistd.h>
#include "../common/parse_args.h"
#include "../common/payload_source.h"
#include "../common/runtime_control.h"
#include "../common/shm_init.h"

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s --size <payload_bytes> [--reset] [--log-interval-ms <ms>] [--shm-name <name>]\n", prog);
    fprintf(stderr, "Example: %s --size 64 --reset --log-interval-ms 1000 --shm-name /shm_ipc_demo\n", prog);
}

int main(int argc, char* argv[]) {
    const char* shm_name = DEFAULT_SHM_NAME;
    uint32_t payload_size = 0;
    uint32_t log_interval_ms = 1000;
    bool reset = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (!require_u32_arg("--size", argv[i + 1], payload_size)) {
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--log-interval-ms") == 0 && i + 1 < argc) {
            if (!require_u32_arg("--log-interval-ms", argv[i + 1], log_interval_ms)) {
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--shm-name") == 0 && i + 1 < argc) {
            shm_name = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "--reset") == 0) {
            reset = true;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (payload_size == 0) {
        usage(argv[0]);
        return 1;
    }

    if (reset) {
        unlink_shm(shm_name);
    }

    SharedMemory* shm = open_shm(payload_size, shm_name);
    if (!shm) return 1;

    RuntimeControl control("producer");

    std::unique_ptr<PayloadSource> payload_source = std::make_unique<XorShiftPayloadSource>();
    uint64_t seq = 0;
    uint64_t total_bytes = 0;
    uint64_t interval_packets = 0;
    uint64_t interval_bytes = 0;
    uint64_t next_log_ns = 0;
    uint64_t interval_started_ns = monotonic_time_ns();
    uint64_t last_packet_ts_ns = 0;

    printf("[producer] started  shm_name=%s  payload_size=%u bytes  log_interval_ms=%u\n", shm_name, payload_size, log_interval_ms);
    control.print_controls();
    printf("\n");

    while (!control.stopped()) {
        control.poll_input();
        if (control.paused()) {
            control.idle_while_paused();
            continue;
        }

        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        if (sem_timedwait(&shm->space_ready, &ts) != 0) {
            if (errno == ETIMEDOUT) {
                continue;
            }
            if (control.stopped()) {
                break;
            }
            perror("sem_timedwait");
            break;
        }

        uint32_t tail = shm->tail.load(std::memory_order_relaxed);
        uint8_t* slot = slot_ptr(shm, tail & RING_MASK);

        uint8_t* data = slot_data(slot);
        payload_source->fill(data, payload_size);

        // Publish the header only after the payload bytes are fully written.
        PacketHeader* hdr = slot_header(slot);
        hdr->sequence     = seq++;
        hdr->timestamp_ns = monotonic_time_ns();
        last_packet_ts_ns = hdr->timestamp_ns;
        hdr->payload_size = payload_size;
        hdr->checksum     = payload_checksum(data, payload_size);

        total_bytes += payload_size;
        ++interval_packets;
        interval_bytes += payload_size;

        // Release-publish the new tail value after the packet contents are ready.
        shm->tail.store(tail + 1, std::memory_order_release);

        if (log_interval_ms == 0 || hdr->timestamp_ns >= next_log_ns) {
            uint64_t elapsed_ns = hdr->timestamp_ns - interval_started_ns;
            double elapsed_sec = elapsed_ns > 0 ? static_cast<double>(elapsed_ns) / 1000000000.0 : 0.0;
            uint64_t packets_per_sec = elapsed_sec > 0.0
                ? static_cast<uint64_t>(static_cast<double>(interval_packets) / elapsed_sec + 0.5)
                : 0;
            uint64_t bytes_per_sec = elapsed_sec > 0.0
                ? static_cast<uint64_t>(static_cast<double>(interval_bytes) / elapsed_sec + 0.5)
                : 0;

            printf("[producer] stats  total_packets=%llu  packets_per_sec=%llu  total_bytes=%llu  bytes_per_sec=%llu  last_packet_ts=%llu\n",
                   static_cast<unsigned long long>(seq),
                   static_cast<unsigned long long>(packets_per_sec),
                   static_cast<unsigned long long>(total_bytes),
                   static_cast<unsigned long long>(bytes_per_sec),
                   static_cast<unsigned long long>(last_packet_ts_ns));
            fflush(stdout);

            interval_packets = 0;
            interval_bytes = 0;
            interval_started_ns = hdr->timestamp_ns;
            next_log_ns = hdr->timestamp_ns + static_cast<uint64_t>(log_interval_ms) * 1000000ull;
        }

        sem_post(&shm->data_ready);
    }

    printf("[producer] final  total_packets=%llu  last_packet_ts=%llu  total_bytes=%llu\n",
           static_cast<unsigned long long>(seq),
           static_cast<unsigned long long>(last_packet_ts_ns),
           static_cast<unsigned long long>(total_bytes));
    fflush(stdout);

    close_shm(shm);
    return 0;
}
