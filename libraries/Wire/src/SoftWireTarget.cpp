/* SoftWire as a target (begin(address)), in a file of its own: only a sketch
 * that calls begin(address) links it, and with it attachInterrupt() and the
 * EXTI handlers (wiring_interrupts.c).
 *
 * One interrupt per transfer: SDA falling while the bus is free is a START,
 * and its handler follows the controller's clock by watching both lines until
 * the STOP. SDA is sampled as SCL rises; a change of SDA while SCL is high is
 * a repeated START (falling) or a STOP (rising). What the next low phase does
 * to SDA - the ACK, letting it go, the next bit of a reply - is worked out in
 * the high phase before it, so the write lands as soon as the fall is seen.
 * Where a callback runs (onRequest() at a read's address, the onReceive() a
 * repeated START owes) SCL is held low first, for as long as it takes (clock
 * stretching). Another target's transfer is followed the same way to its
 * STOP, without touching the bus.
 *
 * Why not an interrupt per edge: on a CH32X035 at 48 MHz the core's EXTI path
 * from the pending flag to the handler measured 5.1-5.4 us (2026-11-15,
 * software-triggered EXTI against SysTick), the whole of a 100 kHz
 * controller's low half. Watching the lines reacts within a turn of the loop.
 * Why not hold SCL at every fall: on that part one open-drain write costs
 * 3-5 us (its pads have no open drain, ch32rv_gpio_od_write() rewrites the
 * pin's configuration), so a hold before the SDA write would double what has
 * to fit in the low half; the write itself is what must land in time.
 *
 * What has to be fast: the START's interrupt, by the end of the first bit's
 * low phase (its hold time plus the low half, about 10 us at 100 kHz); in
 * each high phase, the next low phase's decision; in each low phase, seeing
 * the fall plus one pin write, before SCL rises. The bit loop runs from RAM
 * for that (follow(), about 0.5 KB). Measured at 100 kHz (2026-11-15): the
 * write lands 2.0-3.7 us after the fall on a CH32V003 at 24 MHz, 3.1-4.0 us
 * on a CH32X035 at 48 MHz, against the controller's 5.5 us low half.
 *
 * The cost: the CPU is in this handler for as long as the bus is busy, other
 * targets' transfers included. Every wait is bounded by the bus's timeout
 * (setWireTimeout(); 0 takes the default here, a handler cannot wait for
 * ever): a controller gone mid-transfer lets both lines go and ends it. */
#include "SoftWire.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_ticks.h"
#include "softwire_lines.h"
#include "softwire_target.h"

namespace arduino {

extern void (*softwire_target_stop)(SoftWire *bus);   /* SoftWire.cpp */

using namespace softwire_target;

namespace {

void start_isr(void *bus)
{
    static_cast<SoftWire *>(bus)->startEdge();
}

}  // namespace

void SoftWire::begin(uint16_t address)
{
    begin();
    if (!_started) {
        return;
    }
    _own = (uint8_t)(address & 0x7Fu);
    /* A 10-bit address or the general call: the extended loop
     * (SoftWireTargetExt.cpp), there when the sketch named WIRE_10BIT or
     * called setGeneralCall(). */
    _t_ten = (address & CH32RV_WIRE_10BIT_BIT) != 0;
    _own10 = (uint16_t)(address & 0x3FFu);
    _t_aw = (uint8_t)(_t_ten ? 0xF0u | ((_own10 >> 7) & 0x06u) : _own << 1);
    _t_ext = (_t_ten || _general_call) && softwire_follow_ext;
    _rx_len = 0;
    _rx_read = 0;
    _tx_len = 0;
    _tx_sent = 0;
    _slave = true;
    softwire_target_stop = targetStop;
    /* Only SDA raises interrupts: falling, a START; rising, the bus coming
     * free. SCL is watched, so it may share SDA's EXTI line. A transfer
     * already running is sat out: its edges are flags from before this,
     * dropped here. The bus is taken as free: what this reads here says
     * little (an X035 found its lines not yet both high as the controller's
     * pull-ups came on, and then missed the first transfer). A line that
     * was not free after all - floating, or a transfer under way - costs
     * one wait of the bus's timeout, after which only a START with SCL
     * high, or both lines high, counts again. */
    _t_free = true;
    attachInterruptArg(_sda, start_isr, this, CHANGE);
}

void SoftWire::targetStop(SoftWire *bus)
{
    detachInterrupt(bus->_sda);
    softwire_line_write(bus->_sda, true);
    softwire_line_write(bus->_scl, true);
    bus->_slave = false;
}

/* The 8th clock's rise (the next low phase is the ACK's) and the 9th's (the
 * next is a new byte's first bit): what that low phase does, decided in the
 * high phase before it. In flash: twice a byte, with a high half to do it in. */
__attribute__((noinline)) void SoftWire::byteEdge(bool sampled)
{
    uint8_t act = A_NONE;
    if (_t_bits == 8) {
        if (_t_mode == M_ADDR) {
            if ((uint8_t)(_t_in >> 1) != _own) {
                _t_mode = M_OTHER;             /* another target's: not acknowledged */
            } else if (_t_in & 1u) {
                _t_mode = M_TX;
                act = A_REQUEST;
            } else {
                _t_mode = M_RX;
                _rx_len = 0;
                _rx_read = 0;
                _t_owed = true;
                act = A_LOW;                   /* ACK */
            }
        } else if (_t_mode == M_RX) {
            /* Every byte is acknowledged; past the buffer the head is kept and
             * the tail dropped, as Wire's (and AVR's twi) does. */
            if (_rx_len < CH32RV_WIRE_BUFFER_SIZE) {
                _rx[_rx_len] = _t_in;
                _rx_len = (uint8_t)(_rx_len + 1u);
            }
            act = A_LOW;                       /* ACK */
        } else if (_t_mode == M_TX) {
            act = A_HIGH;                      /* the controller's ACK */
        }
    } else {
        _t_bits = 0;
        _t_in = 0;
        if (_t_mode == M_RX) {
            act = A_HIGH;                      /* this target's ACK ends */
        } else if (_t_mode == M_TX) {
            if (sampled) {
                _t_mode = M_OTHER;             /* NACK: the controller ends with STOP or Sr */
            } else {
                /* Acknowledged (or this target's own ACK of its address): the
                 * next byte - past what onRequest() wrote, 0xFF, the value a
                 * released bus reads as, as Wire's. */
                uint8_t out = 0xFFu;
                if (_tx_sent < _tx_len) {
                    out = _tx[_tx_sent];
                    _tx_sent = (uint8_t)(_tx_sent + 1u);
                }
                _t_out = out;
                act = (out & 0x80u) ? A_HIGH : A_LOW;
            }
        }
    }
    _t_act = act;
}

/* A low phase with a callback in it: SCL held until it returns. */
__attribute__((noinline)) void SoftWire::lowWork(void)
{
    softwire_line_write(_scl, false);
    if (_t_act == A_REQUEST) {
        softwire_line_write(_sda, false);      /* the address's ACK */
        _tx_len = 0;
        _tx_sent = 0;
        if (_on_request) {
            _slave_replying = true;
            _on_request();
            _slave_replying = false;
        }
    } else {
        /* The first fall after a repeated START that ended a write to this
         * target: its onReceive(), before the next address - as Wire's does
         * before onRequest() in a register read. */
        _t_owed = false;
        if (_on_receive) {
            _on_receive((int)_rx_len);
        }
    }
    softwire_line_write(_scl, true);
}

/* The bit loop, in RAM (CH32RV_RAMFUNC): from flash, a CH32X035 at 48 MHz
 * (two wait states) took about 6.5 us from a rise to seeing the next fall,
 * against the 4.6 us high half of a 100 kHz controller. Only what every bit
 * runs is here; the pin writes are the core's, called, and the byte's edges
 * and the callbacks are in flash. */
CH32RV_RAMFUNC uint8_t SoftWire::follow(void)
{
    const pin_size_t scl = CH32RV_PIN_RESOLVE(_scl);
    const pin_size_t sda = CH32RV_PIN_RESOLVE(_sda);
    const Lines l = {(uint8_t)CH32RV_PIN_PORT(scl), (uint8_t)CH32RV_PIN_BIT(scl),
                     (uint8_t)CH32RV_PIN_PORT(sda), (uint8_t)CH32RV_PIN_BIT(sda)};
    /* Where the START's interrupt finds the bus - looked at first, before
     * anything else costs time: the first bit's rise is at most a hold time
     * and a low half away (about 10 us at 100 kHz). */
    bool high = l.scl();
    if (high && l.sda()) {
        /* Both high: the bus is free (SDA rising - the lines coming up, a
         * STOP this target did not follow - or a START already undone). */
        _t_free = true;
        return E_NONE;
    }
    if (!high && !_t_free) {
        /* SCL low: a START taken after SCL fell (SDA may already show the
         * first bit) - but only if the bus was free before it. Otherwise it
         * is a data bit, or lines left floating: a CH32V006 before the
         * controller's pull-ups came on read both low, and each such edge
         * waited out the bus's timeout, starving loop(). */
        return E_NONE;
    }
    bool sampled = false;              /* SDA at the last rise (or the START) */
    _t_mode = M_ADDR;
    _t_bits = 0;
    _t_in = 0;
    _t_act = A_NONE;
    _t_owed = false;
    const uint32_t us = _timeout_us ? _timeout_us : (uint32_t)CH32RV_WIRE_TIMEOUT_US;
    const uint32_t per_us = (uint32_t)(CH32RV_SYSTICK_HZ / 1000000u);
    const uint32_t limit = us > 0xFFFFFFFFu / per_us ? 0xFFFFFFFFu : us * per_us;
    for (;;) {
        if (high) {
            const uint8_t e = highPhase(l, sampled, limit);
            if (e == E_START) {
                /* A repeated START. The write it ends gets onReceive() at the
                 * next fall, where SCL can be held while it runs. */
                _t_mode = M_ADDR;
                _t_bits = 0;
                _t_in = 0;
                _t_act = _t_owed ? A_RECEIVE : A_NONE;
                sampled = false;
                continue;
            }
            if (e != E_FALL) {
                return e;              /* STOP (the caller runs an owed onReceive()), or a timeout */
            }
        }
        /* The low phase: its write, decided in the high phase before. */
        const uint8_t act = _t_act;
        if (act == A_LOW || act == A_HIGH) {
            l.sdaWrite(act == A_HIGH);
        } else if (act != A_NONE) {
            lowWork();
        }
        if (!waitRise(l, limit)) {
            return E_TIMEOUT;
        }
        sampled = l.sda();
        high = true;
        /* The high phase: what the next low phase does. */
        const uint8_t bits = (uint8_t)(_t_bits + 1u);
        _t_bits = bits;
        if (bits <= 8) {
            _t_in = (uint8_t)((_t_in << 1) | (sampled ? 1u : 0u));
        }
        if (bits >= 8) {
            byteEdge(sampled);
        } else if (_t_mode == M_TX) {
            _t_act = ((_t_out >> (7u - bits)) & 1u) ? A_HIGH : A_LOW;   /* the next bit */
        } else {
            _t_act = A_NONE;
        }
    }
}

void SoftWire::startEdge(void)
{
    if (!_slave) {
        return;
    }
    for (;;) {
        const uint8_t end = _t_ext ? softwire_follow_ext(*this) : follow();
        if (end == E_NONE) {
            return;
        }
        _t_free = end == E_STOP;       /* a timeout: the lines are not to be trusted until both read high */
        if (end == E_STOP && _t_owed) {
            /* Only a write ends in onReceive(); a read ends here too, and
             * Wire's (AVR's) does not call it then. */
            _t_owed = false;
            if (_on_receive) {
                _on_receive((int)_rx_len);
            }
        }
        if (end == E_TIMEOUT) {
            /* The controller is gone mid-transfer: let both lines go. */
            _t_owed = false;
            softwire_line_write(_sda, true);
            softwire_line_write(_scl, true);
        }
        /* SDA moved many times during the transfer: those flags are dropped
         * here, and the lines are looked at once more - a START that came
         * while onReceive() ran after the STOP is followed now. */
        attachInterruptArg(_sda, start_isr, this, CHANGE);
        if (end != E_STOP) {
            return;
        }
    }
}

}  // namespace arduino
