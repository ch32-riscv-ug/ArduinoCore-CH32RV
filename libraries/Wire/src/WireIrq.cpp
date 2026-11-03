/* The I2C vectors, in a file of their own: only a target takes I2C
 * interrupts, and CH32RVTwoWire::begin(address) is what links this file, so a
 * controller-only sketch carries no handler. One handler for the EV and ER
 * vectors of every enabled I2C: each I2Cn_EV/ER_IRQHandler is an alias of it,
 * and mcause says which fired. A Disable or Software I2C gets no alias. */
#include "Wire.h"

void ch32rv_wire_dispatch(uint32_t cause);   /* Wire.cpp */

extern "C" __attribute__((interrupt)) void ch32rv_wire_irq(void)
{
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    ch32rv_wire_dispatch(cause);
}
#if CH32RV_I2C_HW_1
extern "C" void I2C1_EV_IRQHandler(void) __attribute__((alias("ch32rv_wire_irq")));
extern "C" void I2C1_ER_IRQHandler(void) __attribute__((alias("ch32rv_wire_irq")));
#endif
#if CH32RV_I2C_HW_2
extern "C" void I2C2_EV_IRQHandler(void) __attribute__((alias("ch32rv_wire_irq")));
extern "C" void I2C2_ER_IRQHandler(void) __attribute__((alias("ch32rv_wire_irq")));
#endif
