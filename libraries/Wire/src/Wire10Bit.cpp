/* 10-bit addressing for the controllers (Wire's and SoftWire's), and the
 * WIRE_10BIT a sketch writes it with (<TwoWire.h>). Naming WIRE_10BIT is what
 * links this file: the transfers reach the two functions below through weak
 * references, so a sketch that never names it carries neither. (A target's
 * 10-bit address is the target's own code: WireTarget.cpp, SoftWireTarget.cpp.)
 *
 * On the wire (UM10204 3.1.11): START, the header 11110 + address bits 9:8 + W,
 * the low byte; a read turns round with a repeated START and the header with
 * R, which addresses the target the write part chose. */
#include "Wire.h"

#include "internal/ch32rv_registers.h"

extern "C" const uint16_t ch32rv_wire_10bit = CH32RV_WIRE_10BIT_BIT;

/* SoftWire's 10-bit target is SoftWireTargetExt.cpp's: naming it here links
 * that file wherever WIRE_10BIT is named (its loop is kept only where a
 * SoftWire target is). */
extern "C" const uint8_t ch32rv_softwire_target_ext;
extern "C" const uint8_t *const ch32rv_wire_10bit_links = &ch32rv_softwire_target_ext;

namespace {
inline uint8_t header_of(uint16_t address)
{
    return (uint8_t)(0xF0u | ((address >> 7) & 0x06u));
}
}  // namespace

/* After SB: the header (ADD10 once it is out), the low byte (ADDR); a read
 * clears that ADDR, repeats the START and sends the header with R. Leaves the
 * last ADDR to the caller as start() does, so the read paths set ACK / POS
 * before clearing it. */
bool arduino::wire_start10(CH32RVTwoWire &bus, uint16_t address, bool read)
{
    const uint32_t base = bus._base;
    CH32RV_I2C_DATAR(base) = header_of(address);
    if (!bus.wait_flag1(CH32RV_I2C_STAR1_ADD10, true)) {
        return false;
    }
    CH32RV_I2C_DATAR(base) = (uint8_t)address;
    if (!bus.wait_flag1(CH32RV_I2C_STAR1_ADDR, true)) {
        return false;
    }
    if (!read) {
        return true;
    }
    (void)CH32RV_I2C_STAR1(base);
    (void)CH32RV_I2C_STAR2(base);          /* ADDR cleared */
    CH32RV_I2C_CTLR1(base) |= CH32RV_I2C_CTLR1_START;
    if (!bus.wait_flag1(CH32RV_I2C_STAR1_SB, true)) {
        return false;
    }
    CH32RV_I2C_DATAR(base) = (uint16_t)(header_of(address) | 1u);
    return bus.wait_flag1(CH32RV_I2C_STAR1_ADDR, true);
}

bool arduino::softwire_send10(SoftWire &bus, uint16_t address, bool read)
{
    if (!bus.write_byte(header_of(address)) || !bus.write_byte((uint8_t)address)) {
        return false;
    }
    if (!read) {
        return true;
    }
    bus.start_condition();
    return bus.write_byte((uint8_t)(header_of(address) | 1u));
}
