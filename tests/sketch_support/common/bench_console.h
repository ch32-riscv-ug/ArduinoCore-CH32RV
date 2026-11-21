/* The test sketches' console (run_hardware.py stages this beside each one).
 *
 * SerialDMSeq by default. Where the build names CONSOLE_UART_RX and
 * CONSOLE_UART_TX (run_hardware.py: the pins of the probe's UART lines,
 * <port>.tx / .rx, for the cases that leave the USARTs alone), Serial on
 * those pins instead, and the monitor reads the probe's fixture UART: the
 * DMSeq monitor polls the debug wire all the time, and on V407 that put
 * +-80 counts on the DAC's PA4 line beside it (+-4 over the UART,
 * 2026-11-21).
 *
 *   CONSOLE_BEGIN(115200);
 *   CONSOLE.println("READY");
 */
#pragma once

#include <Arduino.h>
#include <SerialDMSeq.h>

#if defined(CONSOLE_UART_RX) && defined(CONSOLE_UART_TX)
#define CONSOLE Serial
/* setPins() takes whichever USART has that route (the console's own USART). */
#define CONSOLE_BEGIN(baud)                                \
    do {                                                   \
        Serial.setPins(CONSOLE_UART_RX, CONSOLE_UART_TX);  \
        Serial.begin(baud);                                \
    } while (0)
#else
#define CONSOLE SerialDMSeq
#define CONSOLE_BEGIN(baud) SerialDMSeq.begin(baud)
#endif
