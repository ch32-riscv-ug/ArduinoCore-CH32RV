/* CH32RVSPIClass::setThreeWire() / receive() (SPI.h): the SPI's bidirectional
 * mode, BIDIMODE with BIDIOE choosing the direction. Receiving, the master
 * clocks as long as the SPI is on, so it is turned off one SCK period after
 * the second-to-last byte arrives and the last one then completes - the
 * STM32F1 reference manual's procedure for bidirectional receive-only (25.3.8,
 * the same SPI) - with interrupts off for the count to come out exact. */
#include "SPI.h"
#include "Arduino.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

bool CH32RVSPIClass::setThreeWire(bool on)
{
    if (_periph) {
        return false;
    }
    _three = on;
    apply(_clock_hz, _order, _mode);
    return true;
}

bool CH32RVSPIClass::receive(void *buf, size_t count)
{
    if (!_started || !_three || _periph || !buf) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    const uint32_t b = _base;
    uint8_t *p = static_cast<uint8_t *>(buf);
    const uint16_t on = CH32RV_SPI_CTLR1(b);                 /* sending, SPE on */
    const uint16_t rx = (uint16_t)(on & ~CH32RV_SPI_CTLR1_BIDIOE);
    const uint16_t off = (uint16_t)(rx & ~CH32RV_SPI_CTLR1_SPE);
    /* A little over one SCK period, in CPU cycles: the BR field of what apply()
     * set, against the faster bus clock (the wait has to stay well under the
     * last byte's eight clocks: microseconds were too coarse at 18 MHz). */
    const uint32_t div = 2u << ((on & CH32RV_SPI_CTLR1_BR_MASK) >> CH32RV_SPI_CTLR1_BR_SHIFT);
    const uint32_t pclk = CH32RV_PCLK1_HZ > CH32RV_PCLK2_HZ ? CH32RV_PCLK1_HZ : CH32RV_PCLK2_HZ;
    const uint32_t spins = (uint32_t)((uint64_t)div * (F_CPU / pclk ? F_CPU / pclk : 1u) * 3u / 2u / 4u) + 1u;
    while ((CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_TXE) == 0u) {
    }
    while (CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_BSY) {
    }
    const uint32_t irq = ch32rv_irq_save();
    CH32RV_SPI_CTLR1(b) = (uint16_t)(on & ~CH32RV_SPI_CTLR1_SPE);
    (void)CH32RV_SPI_DATAR(b);
    (void)CH32RV_SPI_STATR(b);
    CH32RV_SPI_CTLR1(b) = off;
    /* The turnaround: the line let go for four SCK periods before the first
     * clock, so a device (or a pull) has it at its level by then - with none,
     * V006 read the last bit sent in the first one or two (2026-11-23). */
    for (volatile uint32_t k = spins * 4u; k; k--) {
    }
    CH32RV_SPI_CTLR1(b) = rx;                                 /* the clock runs from here */
    if (count == 1u) {
        for (volatile uint32_t k = spins; k; k--) {
        }
        CH32RV_SPI_CTLR1(b) = off;
    }
    for (size_t i = 0; i < count; i++) {
        while ((CH32RV_SPI_STATR(b) & CH32RV_SPI_STATR_RXNE) == 0u) {
        }
        p[i] = (uint8_t)CH32RV_SPI_DATAR(b);
        if (i + 2u == count) {
            for (volatile uint32_t k = spins; k; k--) {
            }
            CH32RV_SPI_CTLR1(b) = off;                        /* the last byte completes, then the clock stops */
        }
    }
    ch32rv_irq_restore(irq);
    CH32RV_SPI_CTLR1(b) = (uint16_t)(on & ~CH32RV_SPI_CTLR1_SPE);
    CH32RV_SPI_CTLR1(b) = on;                                 /* sending again: no clock until a byte is written */
    return true;
}

}   // namespace arduino
