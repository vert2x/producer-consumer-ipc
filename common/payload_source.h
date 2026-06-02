#pragma once

#include <cstdint>

class PayloadSource {
public:
    virtual ~PayloadSource() = default;
    virtual void fill(uint8_t* data, uint32_t size) = 0;
};

class RandomPayloadSource final : public PayloadSource {
public:
    void fill(uint8_t* data, uint32_t size) override;
};
