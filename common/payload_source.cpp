#include "payload_source.h"

#include <cstdlib>

void RandomPayloadSource::fill(uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i) {
        data[i] = static_cast<uint8_t>(rand() % 256);
    }
}
