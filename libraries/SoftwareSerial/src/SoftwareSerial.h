/* SoftwareSerial: a UART on any two pins, Arduino's API (AVR's
 * SoftwareSerial: begin, listen, isListening, stopListening, overflow, peek,
 * read, write, available, flush), plus begin(speed, config) for the formats
 * HardwareSerial takes (5-8 data bits, none / even / odd / mark / space
 * parity, 1, 1.5 or 2 stop bits).
 *
 * Receive: the start bit's edge raises the RX pin's interrupt, and its handler
 * reads the rest of the frame there, each bit in its middle by SysTick
 * (ch32rv_ticks()), the interrupt's own delay subtracted (measured by
 * listen(): ch32rv_exti_trigger()). The bytes go to a 64-byte buffer
 * (_SS_MAX_RX_BUFF) that the one listening object owns; a byte that finds it
 * full is dropped and overflow() says so. A frame with a framing or parity
 * error is dropped, as HardwareSerial does.
 *
 * Transmit: bit by bit with interrupts off for the frame (the stop bit with
 * them on). So, as on AVR, it is half duplex: what comes in while a byte goes
 * out is lost, and other interrupts wait up to one frame.
 *
 * The RX pin takes the EXTI line of its bit number: no other pin with that
 * bit number can use attachInterrupt() while this one listens. */
#pragma once

#include <Arduino.h>

#include "internal/ch32rv_ringbuffer.h"

#ifndef _SS_MAX_RX_BUFF
#define _SS_MAX_RX_BUFF 64   /* AVR's size and name */
#endif

class SoftwareSerial : public Stream {
public:
    /* A pin that is no pad (255, or -1 as uint8_t) is none: a SoftwareSerial
     * that only sends, or only receives. inverse_logic: idle low, a start bit
     * high (an RS232 level without a converter's inversion). */
    SoftwareSerial(uint8_t receivePin, uint8_t transmitPin, bool inverse_logic = false);
    ~SoftwareSerial();

    /* 8N1. Then listen(): this object receives from here on. */
    void begin(long speed) { begin(speed, SERIAL_8N1); }
    /* HardwareSerial's SERIAL_8E1, SERIAL_7O1, SERIAL_8N2, ... A speed of 0
     * or less, or a config with another data size, leaves it closed. */
    void begin(long speed, uint16_t config);
    /* Make this the one object that receives: true when it was not before.
     * The other one's buffer is dropped, as on AVR. */
    bool listen();
    void end();
    bool isListening() { return active == this; }
    bool stopListening();
    /* A byte was dropped because the buffer was full (cleared by asking). */
    bool overflow()
    {
        const bool ret = _overflow;
        _overflow = false;
        return ret;
    }
    int peek() override;

    size_t write(uint8_t byte) override;
    int read() override;
    int available() override;
    /* Nothing is held back: write() returns with the byte sent. */
    void flush() override {}
    operator bool() { return true; }

    using Print::write;

    /* For the RX pin's interrupt only. */
    void recv();

private:
    void sendBit(bool high) const;

    static SoftwareSerial *active;
    static CH32RVRingBuffer<_SS_MAX_RX_BUFF> buffer;
    static volatile bool _overflow;

    uint8_t _rx;                 /* pads, or 0xFF for none */
    uint8_t _tx;
    bool _inverse;
    uint8_t _bits = 8;           /* data bits */
    uint8_t _parity = 0;         /* 0 none, 1 even, 2 odd, 3 mark, 4 space */
    /* Bit and stop times in 1/16 SysTick ticks; 0 while not begun. */
    uint32_t _bit_q4 = 0;
    uint32_t _stop_q4 = 0;
    /* SysTick ticks from an edge to this object's interrupt (listen()). */
    uint32_t _latency = 0;
    volatile uint32_t _calibrated_at = 0;
    volatile bool _calibrating = false;
};
