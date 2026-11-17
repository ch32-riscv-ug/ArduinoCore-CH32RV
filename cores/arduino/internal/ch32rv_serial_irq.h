/* The two halves of a USART's interrupt, shared by HardwareSerial.cpp's
 * handler and the modes' (HardwareSerialMode.cpp), inlined into each: the
 * handler must make no call, or its prologue saves every caller-saved
 * register (CH32RVHardwareSerial::irq). For those two files only. */
#pragma once

#include "HardwareSerial.h"
#include "internal/ch32rv_registers.h"

namespace arduino {

/* HardwareSerialMode.cpp's side, which setPins() calls into (the class's
 * friend). Declared here so both files see one definition. */
struct CH32RVSerialMode {
    /* setPins()' CTS / RTS (-1: as it was); false for a pin that is no pad. */
    static bool setControlPins(CH32RVHardwareSerial &port, int cts, int rts);
    /* setPins(p, p): the route found for TX p (USART `index`), RX joined to it. */
    static bool setOneWire(CH32RVHardwareSerial &port, uint8_t index, const ch32rv_route_t &row,
                           int cts, int rts);
    /* CH32RVSerialModeOps' entries. */
    static void start(CH32RVHardwareSerial &port);
    static void end(CH32RVHardwareSerial &port);
    static void irq(CH32RVHardwareSerial &port, uint32_t base);
    static void read(CH32RVHardwareSerial &port);
    static void write(CH32RVHardwareSerial &port);
    static void written(CH32RVHardwareSerial &port);
    /* The CTS / RTS pads (and function numbers) `flow` takes on the port's
     * route; false when one it asks for is not there. */
    static bool flowPads(CH32RVHardwareSerial &port, uint8_t flow, uint8_t &cts, uint8_t &rts,
                         uint8_t &cts_af, uint8_t &rts_af);
    /* One frame at the port's baud and format. */
    static uint32_t frameMicros(CH32RVHardwareSerial &port);
    /* onReceive()'s registration, and its delivery round (attached by
     * HardwareSerialEvent.cpp). */
    static void setOnReceive(CH32RVHardwareSerial &port, const CH32RVCallback<void(void)> &function,
                             bool onlyOnTimeout);
    static size_t deliver(void);
};

__attribute__((always_inline)) inline void CH32RVHardwareSerial::serviceRx(uint32_t base,
                                                                          uint16_t status)
{
    /* Receive by DMA: the data register is the DMA's, and reading it here
     * would take a byte from under it (HardwareSerialDmaRx.cpp reads the
     * errors). */
    if (CH32RV_SERIAL_DMA_ON(*this, RX)) {
        return;
    }
    /* RXNEIE also raises the interrupt for the receive error flags, and those
     * are only cleared by reading STATR and then DATAR. Handling just RXNE
     * leaves an overrun asserted, the interrupt re-enters immediately and the
     * core never returns to loop() - which is what a noisy or unconnected RX
     * line produces. So always drain the data register when any receive flag
     * is set, and only keep the byte when it is a real one: a frame that came
     * with a framing or parity error is a glitch on the line (a probe
     * re-opening its UART put one such byte in front of the next command line,
     * 2026-09-22), not data. Noise alone (NE: the three samples of a bit did
     * not agree) leaves the byte valid - its bits are the samples' majority -
     * and the byte is kept, as AVR, STM32duino and WCH's own core do; it is
     * still reported (UART_NOISE_ERROR). (A good byte, right on the wire, was
     * lost once in about 13 000 at 115200 on CH32X035's Serial1, 2026-11-05;
     * noise alone explains it, the flags were not seen.) An overrun still
     * leaves this byte valid. */
    if (status & (CH32RV_USART_STATR_RXNE | CH32RV_USART_STATR_ORE |
                  CH32RV_USART_STATR_NE | CH32RV_USART_STATR_FE |
                  CH32RV_USART_STATR_PE)) {
        /* The whole register: a break is every bit 0, the parity bit's
         * place too. */
        const uint32_t data = CH32RV_USART_DATAR(base);
        /* The kinds for onReceiveError(), as STATR's own bits (overrun,
         * noise, framing, parity) plus two of ours, read out there. */
        uint32_t marks = status & (CH32RV_USART_STATR_ORE | CH32RV_USART_STATR_NE |
                                   CH32RV_USART_STATR_FE | CH32RV_USART_STATR_PE);
        if (marks & (CH32RV_USART_STATR_FE | CH32RV_USART_STATR_PE)) {
            if ((marks & CH32RV_USART_STATR_FE) && !data) {
                marks ^= CH32RV_USART_STATR_FE | CH32RV_SERIAL_MARK_BREAK;
            }
        } else if ((status & CH32RV_USART_STATR_RXNE) && !_rx.push((uint8_t)(data & _rx_mask))) {
            marks |= CH32RV_SERIAL_MARK_FULL;
        }
        _errors |= (uint8_t)marks;
    }
}

__attribute__((always_inline)) inline void CH32RVHardwareSerial::serviceTx(uint32_t base,
                                                                          uint16_t status)
{
    if (status & CH32RV_USART_STATR_TXE) {
        const int c = _tx.pop();   /* empty: -1, the ring's indices read once */
        if (c < 0) {
            CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TXEIE;
        } else {
            CH32RV_USART_DATAR(base) = (uint16_t)c;
        }
    }
}

}  // namespace arduino
