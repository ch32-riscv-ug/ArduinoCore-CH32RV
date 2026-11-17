/* CH32RVTwoWire::begin(address) and the rest of the target's half, in a file
 * of its own: begin(address) is what links WireIrq.cpp, the I2C vectors -
 * only a target takes interrupts - and with LTO any reference in Wire.cpp,
 * even in a function the sketch never calls, would pull that file in before
 * LTO drops the function (a controller-only sketch carried 372 B of handler
 * on V003). Wire.cpp reaches the rest through weak references (Wire.h), so a
 * controller-only sketch links none of it. */
#include "Wire.h"

#include "internal/ch32rv_registers.h"

extern "C" void ch32rv_wire_irq(void);

using arduino::CH32RVTwoWire;

void CH32RVTwoWire::begin(uint16_t address)
{
    __asm__ volatile("" : : "r"(&ch32rv_wire_irq));
    begin();
    if (_slot) {
        targetStart(address);
    }
}

void CH32RVTwoWire::setGeneralCall(bool enable)
{
    _general_call = enable;
    if (_slave) {
        targetRegs();
    }
}

/* The own address - 7 bits in OADDR1[7:1], or 10 in [9:0] with ADDMODE -
 * the general call, and ACK, which is what makes the peripheral answer its
 * address at all. After begin() and after every reset of the peripheral. */
void CH32RVTwoWire::targetRegs(void)
{
    const uint16_t a = _own;
    CH32RV_I2C_OADDR1(_base) = (uint16_t)((a & CH32RV_WIRE_10BIT_BIT)
        ? (CH32RV_I2C_OADDR1_ADDMODE | CH32RV_I2C_OADDR1_KEEP | (a & 0x3FFu))
        : (CH32RV_I2C_OADDR1_KEEP | ((a & 0x7Fu) << 1)));
    if (_general_call) {
        CH32RV_I2C_CTLR1(_base) |= CH32RV_I2C_CTLR1_ENGC | CH32RV_I2C_CTLR1_ACK;
    } else {
        CH32RV_I2C_CTLR1(_base) = (uint16_t)((CH32RV_I2C_CTLR1(_base) & ~CH32RV_I2C_CTLR1_ENGC) |
                                             CH32RV_I2C_CTLR1_ACK);
    }
}

namespace {
constexpr uint16_t TARGET_IRQS = CH32RV_I2C_CTLR2_ITEVTEN | CH32RV_I2C_CTLR2_ITBUFEN |
                                 CH32RV_I2C_CTLR2_ITERREN;
}

/* A target about to be a controller: wait for the bus to be free with the
 * interrupts on - a transfer to this target is answered meanwhile - then turn
 * them off for the controller's transfer, which polls (the handler would take
 * its SB / TXE / RXNE, and clear its ARLO). Looked at once more with them off:
 * a START that came in between is waited out as well. A START of another
 * controller after this is ours to lose (fail(): ADDR before SB). false, with
 * the interrupts on and the peripheral marked for recovery, when the bus
 * stayed busy for the timeout. */
bool CH32RVTwoWire::targetClaim(void)
{
    const uint32_t t0 = micros();
    for (;;) {
        if (!(CH32RV_I2C_STAR2(_base) & CH32RV_I2C_STAR2_BUSY)) {
            CH32RV_I2C_CTLR2(_base) &= (uint16_t)~TARGET_IRQS;
            if (!(CH32RV_I2C_STAR2(_base) & CH32RV_I2C_STAR2_BUSY)) {
                return true;
            }
            CH32RV_I2C_CTLR2(_base) |= TARGET_IRQS;
        }
        if (_timeout_us != 0 && micros() - t0 > _timeout_us) {
            /* BUSY that outlasts the timeout is a bus held (a target cut off
             * holding SDA, BUSY latched from SCL low): the next transfer
             * resets the peripheral and clears the bus first (claim()), as a
             * controller's failed transfer does. */
            _timeout_flag = true;
            _needs_recovery = true;
            return false;
        }
    }
}

void CH32RVTwoWire::targetRelease(void)
{
    CH32RV_I2C_CTLR2(_base) |= TARGET_IRQS;
}
