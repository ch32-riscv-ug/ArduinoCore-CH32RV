/* SoftSPISlave: CH32RVSPISlave's transfers, clocked bit by bit from pin-change
 * interrupts (SoftSPISlave.h). In a file of its own, so only a sketch that
 * declares one links it, and with it attachInterrupt() and the EXTI
 * handlers. */
#include "SoftSPISlave.h"

#include "internal/ch32rv_deliver.h"
#include "internal/ch32rv_gpio.h"

namespace {

/* The begun slaves, for deliverEvents(). */
SoftSPISlave *first_slave;
ch32rv_delivery_t delivery;

void sck_isr(void *arg)
{
    static_cast<SoftSPISlave *>(arg)->sckEdge();
}

void cs_isr(void *arg)
{
    static_cast<SoftSPISlave *>(arg)->csEdge();
}

}  // namespace

bool SoftSPISlave::begin(int sck, int miso, int mosi, int cs, uint8_t mode, BitOrder order)
{
    end();
    if (sck < 0 || cs < 0 || sck > 0xFE || cs > 0xFE || miso > 0xFE || mosi > 0xFE) {
        return false;
    }
    const int pins[4] = {sck, miso, mosi, cs};
    int pads[4];
    for (int i = 0; i < 4; i++) {
        pads[i] = pins[i] < 0 ? -1 : (int)CH32RV_PIN_RESOLVE(pins[i]);
        if (pads[i] >= 0 && !ch32rv_pad_is_valid(pads[i])) {
            return false;
        }
    }
    /* SCK and CS raise interrupts: one EXTI line each, which is the pin's bit. */
    if (CH32RV_PIN_BIT(pads[0]) == CH32RV_PIN_BIT(pads[3])) {
        return false;
    }
    _sck = (uint8_t)pads[0];
    _cs = (uint8_t)pads[3];
    _miso_port = pads[1] < 0 ? 0xFF : (uint8_t)CH32RV_PIN_PORT(pads[1]);
    _miso_bit = pads[1] < 0 ? 0 : (uint8_t)CH32RV_PIN_BIT(pads[1]);
    _mosi_port = pads[2] < 0 ? 0xFF : (uint8_t)CH32RV_PIN_PORT(pads[2]);
    _mosi_bit = pads[2] < 0 ? 0 : (uint8_t)CH32RV_PIN_BIT(pads[2]);
    _msb = order != LSBFIRST;
    _armed_head = _armed_count = 0;
    _done_head = _done_count = 0;
    _missed = _overruns = 0;
    _selected = false;
    _cur = nullptr;

    pinMode(_sck, INPUT);
    pinMode(_cs, INPUT_PULLUP);                    /* no master: not selected */
    if (pads[2] >= 0) {
        pinMode(pads[2], INPUT);
    }
    if (pads[1] >= 0) {
        pinMode(pads[1], INPUT);                   /* the slave's only while selected */
    }

    _next = first_slave;
    first_slave = this;
    /* The edge both ends sample on: the leading one for CPHA 0, the trailing
     * one for CPHA 1 - rising when CPOL equals CPHA. */
    const bool rising = ((mode >> 1) & 1u) == (mode & 1u);
    attachInterruptArg(_sck, sck_isr, this, rising ? RISING : FALLING);
    attachInterruptArg(_cs, cs_isr, this, CHANGE);
    _begun = true;
    if (digitalRead(_cs) == LOW) {
        csEdge();                                  /* already selected: from here on */
    }
    return true;
}

void SoftSPISlave::end()
{
    if (!_begun) {
        return;
    }
    detachInterrupt(_sck);
    detachInterrupt(_cs);
    if (_miso_port != 0xFF) {
        ch32rv_gpio_set_config(_miso_port, _miso_bit, CH32RV_GPIO_CFG_IN_FLOAT);
    }
    pinMode(_cs, INPUT);
    for (SoftSPISlave **p = &first_slave; *p; p = &(*p)->_next) {
        if (*p == this) {
            *p = _next;
            break;
        }
    }
    _next = nullptr;
    _armed_count = _done_count = 0;
    _selected = false;
    _cur = nullptr;
    _begun = false;
}

inline void SoftSPISlave::miso_bit(bool one)
{
    if (_miso_port == 0xFF) {
        return;
    }
    if (one) {
        ch32rv_gpio_set(_miso_port, _miso_bit);
    } else {
        ch32rv_gpio_clear(_miso_port, _miso_bit);
    }
}

/* The sampling edge: MOSI's bit in, then MISO's next bit up, which the master
 * samples on its next sampling edge (for CPHA 0 the slave would put it up on
 * the trailing edge; any time before the next leading edge will do). */
void SoftSPISlave::sckEdge()
{
    if (!_selected) {
        return;                                    /* another device's transfer */
    }
    uint8_t in = _in;
    uint8_t mask = _mask;
    if (_mosi_port != 0xFF && ch32rv_gpio_read(_mosi_port, _mosi_bit)) {
        in = (uint8_t)(in | mask);
    }
    const uint32_t bits = _bits + 1u;
    _bits = bits;
    if ((bits & 7u) == 0) {
        const Slot *s = _cur;
        const size_t i = _rx_i;
        if (s && s->rx && i < s->size) {
            s->rx[i] = in;
        }
        _rx_i = i + 1u;
        in = 0;
        const size_t t = _tx_i;
        _out = (s && s->tx && t < s->size) ? s->tx[t] : _fill;
        _tx_i = t + 1u;
        mask = _msb ? 0x80u : 0x01u;
    } else {
        mask = _msb ? (uint8_t)(mask >> 1) : (uint8_t)(mask << 1);
    }
    _in = in;
    _mask = mask;
    miso_bit((_out & mask) != 0);
}

void SoftSPISlave::csEdge()
{
    const bool low = ch32rv_gpio_read((uint8_t)CH32RV_PIN_PORT(_cs), (uint8_t)CH32RV_PIN_BIT(_cs)) == 0;
    if (low && !_selected) {
        const Slot *s = _armed_count ? &_armed[_armed_head] : nullptr;
        _cur = s;
        _rx_i = 0;
        _tx_i = 1;
        _bits = 0;
        _in = 0;
        _out = (s && s->tx && s->size) ? s->tx[0] : _fill;
        _mask = _msb ? 0x80u : 0x01u;
        if (_miso_port != 0xFF) {
            /* The first bit on the latch before the pad drives it. */
            miso_bit((_out & _mask) != 0);
            ch32rv_gpio_set_config(_miso_port, _miso_bit, CH32RV_GPIO_CFG_OUT_PP_10M);
        }
        _selected = true;
    } else if (!low && _selected) {
        _selected = false;
        if (_miso_port != 0xFF) {
            ch32rv_gpio_set_config(_miso_port, _miso_bit, CH32RV_GPIO_CFG_IN_FLOAT);
        }
        const uint32_t bits = _bits;
        if (bits) {
            if (bits & 7u) {
                _overruns = _overruns + 1u;
            }
            if (_cur) {
                const uint8_t at = (uint8_t)((_done_head + _done_count) % QUEUE_DEPTH);
                _done[at] = {_cur->rx, (size_t)(bits / 8u), _cur->size};
                _done_count = (uint8_t)(_done_count + 1u);
                _armed_head = (uint8_t)((_armed_head + 1u) % QUEUE_DEPTH);
                _armed_count = (uint8_t)(_armed_count - 1u);
            } else {
                _missed = _missed + 1u;
            }
        }
        _cur = nullptr;
    }
}

bool SoftSPISlave::queue(const uint8_t *tx, uint8_t *rx, size_t size)
{
    if (!_begun) {
        return false;
    }
    noInterrupts();
    /* A finished one holds its place until it is taken, so a result always has room. */
    if (_armed_count + _done_count >= QUEUE_DEPTH) {
        interrupts();
        return false;
    }
    _armed[(_armed_head + _armed_count) % QUEUE_DEPTH] = {tx, rx, size};
    _armed_count = (uint8_t)(_armed_count + 1u);
    interrupts();
    return true;
}

size_t SoftSPISlave::transfer(const uint8_t *tx, uint8_t *rx, size_t size, uint32_t timeout_ms)
{
    if (_cb || _armed_count || _done_count || !queue(tx, rx, size)) {
        return 0;
    }
    const uint32_t t0 = millis();
    Result r;
    while (!poll(r)) {
        if (timeout_ms && (uint32_t)(millis() - t0) >= timeout_ms) {
            noInterrupts();
            const bool withdrawn = !_selected && _armed_count;
            if (withdrawn) {
                _armed_count = 0;
            }
            interrupts();
            if (withdrawn) {
                return 0;
            }
        }
        yield();
    }
    return r.length;
}

bool SoftSPISlave::poll(Result &out)
{
    noInterrupts();
    if (!_done_count) {
        interrupts();
        return false;
    }
    out = _done[_done_head];
    _done_head = (uint8_t)((_done_head + 1u) % QUEUE_DEPTH);
    _done_count = (uint8_t)(_done_count - 1u);
    interrupts();
    return true;
}

void SoftSPISlave::onTransfer(TransferCallback cb)
{
    noInterrupts();
    _cb = cb;
    interrupts();
    /* Attached once, here in normal context, and left so: deliver() looks at
     * every begun slave and runs nothing for one without a callback. */
    if (cb) {
        delivery.deliver = deliver;
        ch32rv_delivery_attach(&delivery);
    }
}

size_t SoftSPISlave::deliver()
{
    size_t ran = 0;
    for (SoftSPISlave *s = first_slave; s; s = s->_next) {
        Result r;
        while (s->_cb && s->poll(r)) {
            s->_cb(r);
            ran++;
        }
    }
    return ran;
}
