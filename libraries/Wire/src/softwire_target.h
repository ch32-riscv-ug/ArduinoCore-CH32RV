/* SoftWire's target: what the plain loop (SoftWireTarget.cpp) and the
 * extended one (SoftWireTargetExt.cpp) share - the lines, the events and
 * modes, the waits. Inline: each loop keeps its own copy, in RAM with it. */
#pragma once

#include "SoftWire.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_ticks.h"

namespace arduino {
namespace softwire_target {

/* The two lines as port and bit, worked out once per transfer. */
struct Lines {
    uint8_t cp, cb, dp, db;

    /* Reads inlined (one load each, in RAM with the loop); writes are the
     * core's, called. */
    bool scl() const { return ch32rv_gpio_read(cp, cb) != 0; }
    bool sda() const { return ch32rv_gpio_read(dp, db) != 0; }
    void sclWrite(bool high) const { ch32rv_gpio_od_write(cp, cb, high); }
    void sdaWrite(bool high) const { ch32rv_gpio_od_write(dp, db, high); }
};

/* What ended a high phase. */
enum : uint8_t {
    E_FALL,        /* SCL fell: the next low phase                            */
    E_START,       /* SDA fell while SCL was high: a (repeated) START         */
    E_STOP,        /* SDA rose while SCL was high                             */
    E_TIMEOUT,     /* nothing moved for the bus's timeout                     */
    E_NONE,        /* no START to follow (follow() at the interrupt's entry)  */
};

/* Who the transfer is for, as far as it has gone. */
enum : uint8_t {
    M_ADDR,        /* the address byte after a START                          */
    M_RX,          /* written to: taking bytes and acknowledging them         */
    M_TX,          /* read from: sending bytes                                */
    M_OTHER,       /* another target's, or a read the controller NACKed       */
    M_ADDR10,      /* the extended loop: a 10-bit header matched, the low byte next */
};

/* What the next low phase does. */
enum : uint8_t {
    A_NONE,
    A_LOW,         /* SDA low: an ACK, or a 0 sent                            */
    A_HIGH,        /* SDA let go: an ACK over, a 1 sent, the controller's ACK */
    A_REQUEST,     /* hold SCL; ACK a read's address and run onRequest()      */
    A_RECEIVE,     /* hold SCL; run the onReceive() a repeated START owes     */
};

/* Turns of the line-watching loops between looks at the clock: the timeout
 * is far longer than a turn, and the loop that does not read SysTick reacts
 * sooner. */
constexpr uint8_t TURNS = 16;

/* SCL is high and SDA reads `sda`: wait for the fall, or a condition. SDA is
 * read before SCL, so a change seen with SCL still high happened while it was
 * high - a data change follows the fall, and would find SCL low. */
inline __attribute__((always_inline)) uint8_t highPhase(const Lines &l, bool sda, uint32_t limit)
{
    const uint32_t since = ch32rv_ticks();
    for (;;) {
        for (uint8_t n = 0; n < TURNS; n++) {
            const bool d = l.sda();
            if (!l.scl()) {
                return E_FALL;
            }
            if (d != sda) {
                return d ? E_STOP : E_START;
            }
        }
        if (ch32rv_ticks() - since > limit) {
            return E_TIMEOUT;
        }
    }
}

inline __attribute__((always_inline)) bool waitRise(const Lines &l, uint32_t limit)
{
    const uint32_t since = ch32rv_ticks();
    for (;;) {
        for (uint8_t n = 0; n < TURNS; n++) {
            if (l.scl()) {
                return true;
            }
        }
        if (ch32rv_ticks() - since > limit) {
            return false;
        }
    }
}

}  // namespace softwire_target
}  // namespace arduino
