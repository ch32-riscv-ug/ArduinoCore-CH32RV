/* Servo for CH32 RISC-V.
 *
 * One timer drives every attached servo in turn: it raises a servo's pin,
 * interrupts when that servo's pulse is over, lowers it and moves on to the
 * next, then idles out the rest of the 20 ms frame. That is how the AVR
 * library does it, and it is why any pin works rather than only the handful a
 * timer's compare output reaches.
 *
 * Which timer is the variant's business (CH32_SERVO_TIMER). It is never the
 * one tone() uses - a sketch may reasonably do both at once - but on the small
 * parts there is no timer left over, and then attaching a servo disturbs
 * analogWrite() on that timer's pads. The variant header says which pads.
 */
#pragma once

#include "Arduino.h"
#include "pins_arduino.h"

#include <stdint.h>

/* The AVR library's numbers, because sketches hard-code them. */
#define MIN_PULSE_WIDTH       544
#define MAX_PULSE_WIDTH      2400
#define DEFAULT_PULSE_WIDTH  1500
#define REFRESH_INTERVAL    20000

/* Twelve is what one AVR timer carries, and it is also what fits a 20 ms frame
 * at the maximum pulse width with room for the frame gap. */
#ifndef CH32_SERVO_MAX
#define CH32_SERVO_MAX 12
#endif

#define SERVOS_PER_TIMER CH32_SERVO_MAX
#define MAX_SERVOS       CH32_SERVO_MAX
#define INVALID_SERVO    255

class Servo {
public:
    Servo();

    /* INVALID_SERVO when the pin is not a pin, when all the slots are taken,
     * or when this series has no timer to spare - the last of which a sketch
     * can also test for at compile time with CH32_SERVO_TIMER. */
    uint8_t attach(int pin);
    uint8_t attach(int pin, int min, int max);
    void detach();

    /* Below 200 the value is an angle, at or above it a pulse width. That
     * ambiguity is the AVR library's, kept so sketches behave the same. */
    void write(int value);
    void writeMicroseconds(int value);
    int read();
    int readMicroseconds();
    bool attached();

private:
    uint8_t _index;      /* slot in the driver's table, or INVALID_SERVO */
    int16_t _min;        /* pulse width for write(0)   */
    int16_t _max;        /* pulse width for write(180) */
};
