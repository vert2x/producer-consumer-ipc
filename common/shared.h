#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <semaphore.h>
#include <time.h>

// ---- Packet header ---------------------------------------------------------

struct PacketHeader {
    uint64_t sequence;       // sequence number
    uint64_t timestamp_ns;   // producer timestamp in CLOCK_MONOTONIC nanoseconds
    uint32_t payload_size;   // payload size in bytes
    uint32_t checksum;       // payload checksum
};

// ---- Ring buffer -----------------------------------------------------------

static constexpr uint32_t RING_CAPACITY = 8;   // power of two
static constexpr uint32_t RING_MASK     = RING_CAPACITY - 1;
static constexpr uint32_t SHM_MAGIC     = 0xDEADBEEF;

struct SharedMemory {
    uint32_t magic;           // initialization flag
    uint32_t generation;      // increments whenever the SHM region is recreated
    uint32_t payload_size;    // size of one packet payload — set at startup

    std::atomic<uint32_t> head;  // moved by consumer
    std::atomic<uint32_t> tail;  // moved by producer

    sem_t data_ready;         // producer → consumer: data available
    sem_t space_ready;        // consumer → producer: space available

    // slots start immediately after this header in memory
    // layout: [ PacketHeader | uint8_t data[payload_size] ] × RING_CAPACITY
};

// ---- helper functions for slot addressing ---------------------------------

// size of a single slot in bytes
inline size_t slot_size(uint32_t payload_size) {
    return sizeof(PacketHeader) + payload_size;
}

// total size of SHM region
inline size_t shm_size(uint32_t payload_size) {
    return sizeof(SharedMemory) + RING_CAPACITY * slot_size(payload_size);
}

// pointer to slot start by index
inline uint8_t* slot_ptr(SharedMemory* shm, uint32_t index) {
    uint8_t* slots_begin = reinterpret_cast<uint8_t*>(shm) + sizeof(SharedMemory);
    return slots_begin + index * slot_size(shm->payload_size);
}

// packet header from slot pointer
inline PacketHeader* slot_header(uint8_t* slot) {
    return reinterpret_cast<PacketHeader*>(slot);
}

// pointer to payload from slot pointer
inline uint8_t* slot_data(uint8_t* slot) {
    return slot + sizeof(PacketHeader);
}

inline uint64_t monotonic_time_ns() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

// FNV-1a
inline uint32_t payload_checksum(const uint8_t* data, uint32_t size) {
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

inline void print_payload_preview(const uint8_t* data, uint32_t size, uint32_t edge_bytes = 4) {
    if (size == 0) {
        printf("empty");
        return;
    }

    uint32_t shown = (size <= edge_bytes * 2) ? size : edge_bytes;
    for (uint32_t i = 0; i < shown; ++i) {
        printf("%s%02x", (i == 0 ? "" : " "), static_cast<unsigned>(data[i]));
    }

    if (size <= edge_bytes * 2) {
        return;
    }

    printf(" ...");
    for (uint32_t i = size - edge_bytes; i < size; ++i) {
        printf(" %02x", static_cast<unsigned>(data[i]));
    }
}

static constexpr const char* DEFAULT_SHM_NAME = "/shm_ipc_default";
