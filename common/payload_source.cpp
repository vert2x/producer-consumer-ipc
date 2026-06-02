#include "payload_source.h"

namespace {
uint32_t next_xorshift32(uint32_t& state) {
    if (state == 0) {
        state = 0x12345678u;
    }

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
}  // namespace

XorShiftPayloadSource::XorShiftPayloadSource(uint32_t seed)
    : state_(seed == 0 ? 0x12345678u : seed) {}

void XorShiftPayloadSource::fill(uint8_t* data, uint32_t size) {
    uint32_t word = 0;
    for (uint32_t i = 0; i < size; ++i) {
        if ((i & 3u) == 0) {
            word = next_xorshift32(state_);
        }
        data[i] = static_cast<uint8_t>(word >> ((i & 3u) * 8u));
    }
}
