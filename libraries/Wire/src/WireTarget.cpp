/* CH32RVTwoWire::begin(address), in a file of its own: it is what links
 * WireIrq.cpp, the I2C vectors - only a target takes interrupts - and with
 * LTO any reference in Wire.cpp, even in a function the sketch never calls,
 * would pull that file in before LTO drops the function (a controller-only
 * sketch carried 372 B of handler on V003). */
#include "Wire.h"

extern "C" void ch32rv_wire_irq(void);

void arduino::CH32RVTwoWire::begin(uint8_t address)
{
    __asm__ volatile("" : : "r"(&ch32rv_wire_irq));
    begin();
    if (_slot) {
        targetStart(address);
    }
}
