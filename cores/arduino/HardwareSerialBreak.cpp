/* HardwareSerial::sendBreak() (HardwareSerial.h): CTLR1's SBK, set after the
 * last byte written has gone out (flush()); the USART clears it during the
 * break's stop bit. A break is a frame of zeros and its stop bit, 13 bits in
 * the LIN mode (CTLR2 LINEN, setMode(UART_MODE_LIN)). Its own file: a sketch
 * that never calls it links none of it. */
#include "HardwareSerial.h"

#include "Arduino.h"
#include "internal/ch32rv_registers.h"

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

bool CH32RVHardwareSerial::sendBreak()
{
    if (!_slot) {
        return false;
    }
    flush();
    const uint32_t base = registers();
    CH32RV_USART_CTLR1(base) |= CH32RV_USART_CTLR1_SBK;
    /* a break is at most 14 bits: at 300 baud 47 ms */
    const uint32_t t0 = millis();
    while (CH32RV_USART_CTLR1(base) & CH32RV_USART_CTLR1_SBK) {
        if (millis() - t0 > 100u) {
            CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_SBK;
            return false;
        }
    }
    /* SBK goes back to 0 as the break's stop bit (the delimiter) begins, and
     * TC was already set by flush(): the delimiter's whole bit waited for
     * here, with a bit's margin, before the next byte's start bit */
    const uint32_t baud = baudRate();
    delayMicroseconds(baud ? (2000000u + baud - 1u) / baud : 0u);
    return true;
}

#endif
