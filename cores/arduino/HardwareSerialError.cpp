/* Serial's onReceiveError() (HardwareSerial.h): ESP32's receive error
 * callback. In a file of its own so that only a sketch that calls it links the
 * callbacks and their delivery; the interrupt marks the kinds anyway (one byte
 * per port, HardwareSerial.cpp). Delivered after loop(), with serialEvent(). */
#include "HardwareSerial.h"
#include "internal/ch32rv_registers.h"

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

extern void (*ch32rv_serial_error_delivery)(void);

namespace {

struct Registration {
    CH32RVHardwareSerial *port;
    CH32RVCallback<void(hardwareSerial_error_t)> callback;
};
Registration registrations[CH32RV_SERIAL_COUNT];

void deliver(void)
{
    for (Registration &r : registrations) {
        if (!r.port || !r.callback) {
            continue;
        }
        static const uint8_t kinds[][2] = {
            {CH32RV_SERIAL_MARK_BREAK, UART_BREAK_ERROR},
            {CH32RV_SERIAL_MARK_FULL, UART_BUFFER_FULL_ERROR},
            {CH32RV_USART_STATR_ORE, UART_FIFO_OVF_ERROR},
            {CH32RV_USART_STATR_FE, UART_FRAME_ERROR},
            {CH32RV_USART_STATR_PE, UART_PARITY_ERROR},
        };
        const uint8_t errors = r.port->takeErrors();
        for (const auto &kind : kinds) {
            if (errors & kind[0]) {
                r.callback((hardwareSerial_error_t)kind[1]);
            }
        }
    }
}

}  // namespace

void CH32RVHardwareSerial::onReceiveError(CH32RVCallback<void(hardwareSerial_error_t)> function)
{
    if (number() >= CH32RV_SERIAL_COUNT) {
        return;
    }
    takeErrors();                  /* what came before is not this callback's */
    registrations[number()] = Registration{this, function};
    ch32rv_serial_error_delivery = deliver;
}

#endif
