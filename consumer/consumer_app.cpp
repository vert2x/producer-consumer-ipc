#include <cstdio>
#include <unistd.h>
#include "../common/shm_init.h"

int main() {
    SharedMemory* shm = open_shm(0);
    if (!shm) return 1;

    printf("[consumer] started, waiting for packets\n\n");

    while (true) {
        // Wait until at least one packet becomes available
        sem_wait(&shm->data_ready);

        uint32_t head = shm->head.load(std::memory_order_relaxed);
        // uint8_t slot = head & RING_MASK;
        uint8_t* slot = slot_ptr(shm, head & RING_MASK);

        // acquire: see everything the producer wrote before its release
        // Packet pkt = shm->slots[slot];
        // shm->head.store(head + 1, std::memory_order_release);
        PacketHeader* hdr = slot_header(slot);
        auto data = slot_data(slot);


        printf("[consumer] received seq=%-4lu  value=%s\n",
               hdr->sequence, data);
        fflush(stdout);

        sem_post(&shm->space_ready);
    }
}
