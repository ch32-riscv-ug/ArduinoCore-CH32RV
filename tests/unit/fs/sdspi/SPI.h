/* The host build's SPI.h: only SPIClass, which the simulated card (sdcard_sim.h) is. */
#pragma once
#include "Arduino.h"
#include "api/HardwareSPI.h"

class SPIClass : public arduino::HardwareSPI {
public:
    uint16_t transfer16(uint16_t data) override
    {
        const uint8_t hi = transfer((uint8_t)(data >> 8));
        return (uint16_t)((hi << 8) | transfer((uint8_t)data));
    }
    void transfer(void *buf, size_t count) override
    {
        uint8_t *p = (uint8_t *)buf;
        for (size_t i = 0; i < count; i++) {
            p[i] = transfer(p[i]);
        }
    }
    using arduino::HardwareSPI::transfer;
    void writeBytes(const uint8_t *data, uint32_t size)
    {
        for (uint32_t i = 0; i < size; i++) {
            transfer(data[i]);
        }
    }
    void usingInterrupt(int) override {}
    void notUsingInterrupt(int) override {}
    void attachInterrupt() override {}
    void detachInterrupt() override {}
    void begin() override {}
    void end() override {}
};
extern SPIClass &SPI;
