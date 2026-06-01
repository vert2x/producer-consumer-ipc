#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include "../common/shm_init.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int) {
    g_stop = 1;
}

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s --size <payload_bytes> [--reset]\n", prog);
    fprintf(stderr, "Example: %s --size 64 --reset\n", prog);
}


int main(int argc, char* argv[]) {
    uint32_t payload_size = 0;
    bool reset = false;

       // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            payload_size = static_cast<uint32_t>(atoi(argv[i + 1]));
            i++;
        } else if (strcmp(argv[i], "--reset") == 0) {
            reset = true;
        }
    }

    if (payload_size == 0) {
         usage(argv[0]);
         return 1;
     }

     if (reset) {
         unlink_shm();
     }

     SharedMemory* shm = open_shm(payload_size);
     if (!shm) return 1;

     std::signal(SIGINT, handle_signal);
     std::signal(SIGTERM, handle_signal);

     srand(static_cast<unsigned>(time(nullptr)));
     uint64_t seq = 0;

     printf("[producer] started  payload_size=%u bytes\n\n", payload_size);

     while (!g_stop) {
         timespec ts{};
         clock_gettime(CLOCK_REALTIME, &ts);
         ts.tv_sec += 1;

         if (sem_timedwait(&shm->space_ready, &ts) != 0) {
             if (errno == ETIMEDOUT) {
                 continue;
             }
             if (g_stop) {
                 break;
             }
             perror("sem_timedwait");
             break;
         }

         uint32_t tail = shm->tail.load(std::memory_order_relaxed);
         uint8_t* slot = slot_ptr(shm, tail & RING_MASK);

         // Fill the header
         PacketHeader* hdr = slot_header(slot);
         hdr->sequence     = seq++;
         hdr->payload_size = payload_size;

         // Fill the payload with random bytes
         uint8_t* data = slot_data(slot);
         for (uint32_t i = 0; i < payload_size; i++) {
             data[i] = static_cast<uint8_t>(rand() % 256);
         }

         shm->tail.store(tail + 1, std::memory_order_release);

         // Print the first payload bytes for clarity
         printf("[producer] sent  seq=%-4lu  size=%u  data=[%d, %d, %d ...]\n",
                hdr->sequence, payload_size, data[0], data[1], data[2]);
         fflush(stdout);

         sem_post(&shm->data_ready);
         sleep(1);
     }

     close_shm(shm);
     return 0;
}
