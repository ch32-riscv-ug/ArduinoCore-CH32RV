/* SoftWire's extended target - a 10-bit address (WIRE_10BIT), the general
 * call (setGeneralCall()) - in a file of its own, with a bit loop of its own
 * (followExt()): begin(address) chooses it, so a plain
 * 7-bit target runs the loop SoftWireTarget.cpp always had, cycle for cycle
 * (a V003 at 24 MHz missed ACKs at 100 kHz with both in one loop,
 * 2026-11-17). SoftWireTarget.cpp reaches it through a weak reference
 * (softwire_follow_ext); setGeneralCall() is here, and Wire10Bit.cpp - which
 * naming WIRE_10BIT links - names this file, so only those sketches link it,
 * and only those with a SoftWire target keep its loop (LTO drops it
 * otherwise).
 *
 * The extended loop runs from flash, not RAM: two loops of about 0.5 KB in
 * RAM did not fit a CH32V003 (2 KB) beside the test sketch, and a part that
 * names WIRE_10BIT for its hardware Wire would have carried it. From flash a
 * part with flash wait states follows slower controllers (X035 at 48 MHz:
 * about 6.5 us from a rise to seeing the next fall, 2026-11-15): the
 * extended target is for clocks up to about 50 kHz there. */
#include "SoftWire.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_ticks.h"
#include "softwire_lines.h"
#include "softwire_target.h"

/* Named by Wire10Bit.cpp, so that naming WIRE_10BIT links this file. */
extern "C" const uint8_t ch32rv_softwire_target_ext = 1;

namespace arduino {

using namespace softwire_target;

void SoftWire::setGeneralCall(bool enable)
{
    _general_call = enable;
    if (_slave) {
        _t_ext = _t_ten || enable;
    }
}

uint8_t softwire_follow_ext(SoftWire &bus)
{
    return bus.followExt();
}

/* A write addressed to this target: ACK it, and onReceive() is owed. */
inline __attribute__((always_inline)) uint8_t SoftWire::acceptWrite(void)
{
    _t_mode = M_RX;
    _rx_len = 0;
    _rx_read = 0;
    _t_owed = true;
    return A_LOW;
}

/* The 8th clock's rise (the next low phase is the ACK's) and the 9th's (the
 * next is a new byte's first bit): what that low phase does, decided in the
 * high phase before it. In flash: twice a byte, with a high half to do it in. The extended
 * loop's: a 10-bit address, the general call. */
__attribute__((noinline)) void SoftWire::byteEdgeExt(bool sampled)
{
    uint8_t act = A_NONE;
    if (_t_bits == 8) {
        if (_t_mode == M_ADDR) {
            /* Its address byte with W (7-bit: the address, 10-bit: the
             * header), worked out in begin(): one compare, in the high half
             * before the ACK's low phase. */
            const uint8_t a = _t_in;
            if ((uint8_t)(a & 0xFEu) == _t_aw) {
                if (!(a & 1u)) {
                    if (_t_ten) {
                        _t_mode = M_ADDR10;
                        act = A_LOW;           /* the header's ACK; the low byte next */
                    } else {
                        act = acceptWrite();
                    }
                } else if (!_t_ten || _t_10) {
                    /* (a 10-bit header with R: only after this target's full
                     * address since the START - a 10-bit read's repeated START) */
                    _t_mode = M_TX;
                    act = A_REQUEST;
                } else {
                    _t_mode = M_OTHER;
                }
            } else if (a == 0 && _general_call) {
                act = acceptWrite();           /* the general call */
            } else {
                _t_mode = M_OTHER;             /* another target's: not acknowledged */
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
        } else if (_t_mode == M_ADDR10) {
            if (_t_in == (uint8_t)_own10) {
                _t_10 = true;
                act = acceptWrite();
            } else {
                _t_mode = M_OTHER;             /* another 10-bit target's */
            }
        }
    } else {
        _t_bits = 0;
        _t_in = 0;
        if (_t_mode == M_RX || _t_mode == M_ADDR10) {
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

uint8_t SoftWire::followExt(void)
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
    _t_10 = false;
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
                /* (A 10-bit read is a write of the address alone, turned
                 * round: addressing, not a message - no onReceive(0).) */
                if (_t_10 && _rx_len == 0) {
                    _t_owed = false;
                }
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
            /* A callback: the clock held first, then (a read's address) the
             * ACK, here in RAM - from lowWork() in flash the ACK came after
             * the controller sampled it on a V006 (2026-11-17). */
            l.sclWrite(false);
            if (act == A_REQUEST) {
                l.sdaWrite(false);
            }
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
            byteEdgeExt(sampled);
        } else if (_t_mode == M_TX) {
            _t_act = ((_t_out >> (7u - bits)) & 1u) ? A_HIGH : A_LOW;   /* the next bit */
        } else {
            _t_act = A_NONE;
        }
    }
}

}  // namespace arduino
