#pragma once
#include "shared.h"

// payload_size > 0  — producer passes the size from argv
// payload_size == 0 — consumer passes 0 (the size is read from the existing SHM)
SharedMemory* open_shm(uint32_t payload_size);

// Unmap the shared memory region from the current process.
void close_shm(SharedMemory* shm);

// Remove the named shared memory object from the system.
// Existing mappings remain valid until the last process unmaps them.
bool unlink_shm();
