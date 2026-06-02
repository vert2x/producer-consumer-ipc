#pragma once

#include <cstdint>

class PayloadSource {
public:
    virtual ~PayloadSource() = default;
    virtual void fill(uint8_t* data, uint32_t size) = 0;
};

class XorShiftPayloadSource final : public PayloadSource {
public:
    explicit XorShiftPayloadSource(uint32_t seed = 0x12345678u);
    void fill(uint8_t* data, uint32_t size) override;

private:
    uint32_t state_;
};
