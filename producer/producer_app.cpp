#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

       // Parse command-line arguments
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

     srand(static_cast<unsigned>(time(nullptr)));
     std::unique_ptr<PayloadSource> payload_source = std::make_unique<RandomPayloadSource>();
     uint64_t seq = 0;
     uint64_t next_log_ns = 0;

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

         // Fill the header after the payload is ready
         PacketHeader* hdr = slot_header(slot);
         hdr->sequence     = seq++;
         hdr->timestamp_ns = monotonic_time_ns();
         hdr->payload_size = payload_size;
         hdr->checksum     = payload_checksum(data, payload_size);

         shm->tail.store(tail + 1, std::memory_order_release);

         if (log_interval_ms == 0 || hdr->timestamp_ns >= next_log_ns) {
             printf("[producer] sent  seq=%-4lu  ts=%llu  csum=%08x  size=%u  data=[",
                    hdr->sequence,
                    static_cast<unsigned long long>(hdr->timestamp_ns),
                    hdr->checksum,
                    payload_size);
             print_payload_preview(data, payload_size);
             printf("]\n");
             fflush(stdout);
             next_log_ns = hdr->timestamp_ns + static_cast<uint64_t>(log_interval_ms) * 1000000ull;
         }

         sem_post(&shm->data_ready);
     }

     close_shm(shm);
     return 0;
}
