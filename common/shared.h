#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <semaphore.h>

// ---- Packet header ---------------------------------------------------------

struct PacketHeader {
    uint64_t sequence;      // sequence number
    uint32_t payload_size;  // payload size in bytes
};

// ---- Ring buffer -----------------------------------------------------------

static constexpr uint32_t RING_CAPACITY = 8;   // power of two
static constexpr uint32_t RING_MASK     = RING_CAPACITY - 1;
static constexpr uint32_t SHM_MAGIC     = 0xDEADBEEF;

struct SharedMemory {
    uint32_t magic;           // initialization flag
    uint32_t generation;      // increments whenever the SHM region is recreated
    uint32_t payload_size;    // size of one packet payload — set at startup

    // alignas(64) std::atomic<uint32_t> head;  // moved by consumer
    // alignas(64) std::atomic<uint32_t> tail;  // moved by producer
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

static constexpr const char* SHM_NAME = "/shm_demo";
