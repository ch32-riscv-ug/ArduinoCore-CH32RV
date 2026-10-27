/* CH32RVSPISlave: the SPI in slave mode with software slave select (SSM),
 * SSI driven from the CS pin's EXTI interrupt, every byte moved by the SPI's
 * own interrupt (RXNE in, TXE out).
 *
 * Between transfers the SPI is reset through RCC (PRSTR, 0x0C below the
 * clock enable register with the same bit, on every SPI of every family in
 * ch32-device-data's clock_enables.csv / registers.csv, 2026-10-28): the
 * transmit side runs one or two bytes ahead of the master, and a reset is the
 * one way to drop them on every family, so the next transfer starts with its
 * own first byte, already loaded before CS falls.
 *
 * LSBFIRST is done here, not by the SPI: on CH32V003 and CH32X035 a slave
 * with CTLR1.LSBFIRST receives right but transmits the first bit of each
 * byte for all eight (measured 2026-10-28; V103 / V307 transmit right). The
 * SPI always runs MSB first and the bytes are reversed on the way in and out. */
#include "CH32RVSPISlave.h"

#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"

#define CTLR2_ERRIE   (1u << 5)
#define CTLR2_RXNEIE  (1u << 6)
#define CTLR2_TXEIE   (1u << 7)
#define SPI_RSTR(c)   CH32RV_REG32((c).clken_addr - 0x0Cu)

namespace {

/* By SPI number, for the handlers and deliverEvents(). */
CH32RVSPISlave *by_number[4];

ch32rv_delivery_t delivery;

void cs_isr(void *arg)
{
    static_cast<CH32RVSPISlave *>(arg)->csEdge();
}

int irqn_of(uint8_t number)
{
    switch (number) {
#ifdef CH32RV_IRQN_SPI1
    case 1: return CH32RV_IRQN_SPI1;
#endif
#ifdef CH32RV_IRQN_SPI2
    case 2: return CH32RV_IRQN_SPI2;
#endif
#ifdef CH32RV_IRQN_SPI3
    case 3: return CH32RV_IRQN_SPI3;
#endif
    default: return -1;
    }
}

uint8_t reverse(uint8_t b)
{
    b = (uint8_t)((b & 0xF0u) >> 4 | (b & 0x0Fu) << 4);
    b = (uint8_t)((b & 0xCCu) >> 2 | (b & 0x33u) << 2);
    return (uint8_t)((b & 0xAAu) >> 1 | (b & 0x55u) << 1);
}

void pad(uint8_t pin, uint8_t config)
{
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin), config);
}

}  // namespace

bool CH32RVSPISlave::begin(int sck, int miso, int mosi, int cs, uint8_t mode, BitOrder order)
{
    end();
    if (cs < 0 || cs > 0xFE) {
        return false;
    }
    if (!ch32rv_spi_claim(this, sck, miso, mosi, &_spi)) {
        return false;
    }
    if (irqn_of(_spi.number) < 0) {
        ch32rv_spi_release(this, &_spi);
        return false;
    }
    _cs = (uint8_t)CH32RV_PIN_RESOLVE(cs);
    _mode = (uint8_t)(mode & 3u);
    _order = order;
    _armed_head = _armed_count = 0;
    _done_head = _done_count = 0;
    _missed = _overruns = 0;
    _selected = false;
    _cur = nullptr;

    pad(_spi.sck, CH32RV_GPIO_CFG_IN_FLOAT);
    pad(_spi.mosi, CH32RV_GPIO_CFG_IN_FLOAT);
    pad(_spi.miso, CH32RV_GPIO_CFG_IN_FLOAT);     /* the slave's only while selected */
    pinMode(_cs, INPUT_PULLUP);                    /* no master: not selected */

    by_number[_spi.number] = this;
    restart();
    ch32rv_irq_enable((uint32_t)irqn_of(_spi.number));
    attachInterruptArg(_cs, cs_isr, this, CHANGE);
    _begun = true;
    if (digitalRead(_cs) == LOW) {
        csEdge();                                  /* already selected: from here on */
    }
    return true;
}

void CH32RVSPISlave::end()
{
    if (!_begun) {
        return;
    }
    detachInterrupt(_cs);
    ch32rv_irq_disable((uint32_t)irqn_of(_spi.number));
    ch32rv_spi_release(this, &_spi);
    by_number[_spi.number] = nullptr;
    pinMode(_cs, INPUT);
    _armed_count = _done_count = 0;
    _selected = false;
    _cur = nullptr;
    _begun = false;
}

/* Not selected: the SPI from reset, slave, NSS high, the next transfer's
 * first byte already in the transmit buffer. */
void CH32RVSPISlave::restart()
{
    SPI_RSTR(_spi) |= _spi.clken_mask;
    SPI_RSTR(_spi) &= ~_spi.clken_mask;
    uint16_t ctlr1 = CH32RV_SPI_CTLR1_SSM | CH32RV_SPI_CTLR1_SSI;
    if (_mode & 1u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPHA;
    }
    if (_mode & 2u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPOL;
    }
    CH32RV_SPI_CTLR1(_spi.base) = ctlr1;
    CH32RV_SPI_CTLR1(_spi.base) = ctlr1 | CH32RV_SPI_CTLR1_SPE;
    const Slot *next = _armed_count ? &_armed[_armed_head] : nullptr;
    const uint8_t first = (next && next->tx && next->size) ? next->tx[0] : _fill;
    CH32RV_SPI_DATAR(_spi.base) = _order == LSBFIRST ? reverse(first) : first;
    _tx_i = 1;
}

void CH32RVSPISlave::csEdge()
{
    const bool low = digitalRead(_cs) == LOW;
    if (low && !_selected) {
        _cur = _armed_count ? &_armed[_armed_head] : nullptr;
        _rx_i = 0;
        _selected = true;
        pad(_spi.miso, CH32RV_GPIO_CFG_AF_PP_50M);
        CH32RV_SPI_CTLR1(_spi.base) &= (uint16_t)~CH32RV_SPI_CTLR1_SSI;
        CH32RV_SPI_CTLR2(_spi.base) = CTLR2_ERRIE | CTLR2_RXNEIE | CTLR2_TXEIE;
    } else if (!low && _selected) {
        CH32RV_SPI_CTLR2(_spi.base) = 0;
        isr();                                     /* the last byte, if its interrupt has not run */
        CH32RV_SPI_CTLR1(_spi.base) |= CH32RV_SPI_CTLR1_SSI;
        pad(_spi.miso, CH32RV_GPIO_CFG_IN_FLOAT);
        _selected = false;
        const size_t n = _rx_i;
        if (n) {
            if (_cur) {
                const uint8_t at = (uint8_t)((_done_head + _done_count) % QUEUE_DEPTH);
                _done[at] = {_cur->rx, n, _cur->size};
                _done_count = (uint8_t)(_done_count + 1u);
                _armed_head = (uint8_t)((_armed_head + 1u) % QUEUE_DEPTH);
                _armed_count = (uint8_t)(_armed_count - 1u);
                if (_cb) {
                    ch32rv_delivery_attach(&delivery);
                }
            } else {
                _missed = _missed + 1u;
            }
        }
        _cur = nullptr;
        restart();
    }
}

void CH32RVSPISlave::isr()
{
    const uint16_t st = CH32RV_SPI_STATR(_spi.base);
    if (st & CH32RV_SPI_STATR_RXNE) {
        uint8_t b = (uint8_t)CH32RV_SPI_DATAR(_spi.base);
        if (_order == LSBFIRST) {
            b = reverse(b);
        }
        const Slot *s = _cur;
        const size_t i = _rx_i;
        if (s && s->rx && i < s->size) {
            s->rx[i] = b;
        }
        _rx_i = i + 1u;
    }
    if (st & CH32RV_SPI_STATR_OVR) {
        _overruns = _overruns + 1u;
        (void)CH32RV_SPI_STATR(_spi.base);         /* DATAR then STATR clears it */
    }
    if ((st & CH32RV_SPI_STATR_TXE) && (CH32RV_SPI_CTLR2(_spi.base) & CTLR2_TXEIE)) {
        const Slot *s = _cur;
        const size_t i = _tx_i;
        const uint8_t b = (s && s->tx && i < s->size) ? s->tx[i] : _fill;
        CH32RV_SPI_DATAR(_spi.base) = _order == LSBFIRST ? reverse(b) : b;
        _tx_i = i + 1u;
    }
}

void CH32RVSPISlave::setFill(uint8_t fill)
{
    noInterrupts();
    _fill = fill;
    if (_begun && !_selected && !_armed_count) {
        restart();                                 /* the old fill byte was loaded */
    }
    interrupts();
}

bool CH32RVSPISlave::queue(const uint8_t *tx, uint8_t *rx, size_t size)
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
    if (_armed_count == 1u && !_selected) {
        restart();                                 /* the fill byte was loaded; load this one's first */
    }
    interrupts();
    return true;
}

size_t CH32RVSPISlave::transfer(const uint8_t *tx, uint8_t *rx, size_t size, uint32_t timeout_ms)
{
    if (_cb || numTransactionsInFlight() || available() || !queue(tx, rx, size)) {
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
                restart();
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

bool CH32RVSPISlave::poll(Result &out)
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

void CH32RVSPISlave::onTransfer(TransferCallback cb, void *arg)
{
    noInterrupts();
    _cb = cb;
    _cb_arg = arg;
    interrupts();
    if (delivery.deliver == nullptr) {
        delivery.deliver = deliver;
    }
    if (cb && _done_count) {
        ch32rv_delivery_attach(&delivery);
    }
}

size_t CH32RVSPISlave::deliver()
{
    size_t ran = 0;
    for (CH32RVSPISlave *s : by_number) {
        if (!s || !s->_cb) {
            continue;
        }
        Result r;
        while (s->_cb && s->poll(r)) {
            s->_cb(r, s->_cb_arg);
            ran++;
        }
    }
    return ran;
}

uint8_t CH32RVSPISlave::numTransactionsInFlight() const
{
    return _armed_count;
}

uint8_t CH32RVSPISlave::available() const
{
    return _done_count;
}

#define CH32RV_SPI_SLAVE_HANDLER(n)                                            \
    extern "C" __attribute__((interrupt)) void SPI##n##_IRQHandler(void)       \
    {                                                                          \
        if (by_number[n]) {                                                    \
            by_number[n]->isr();                                               \
        }                                                                      \
    }
#ifdef CH32RV_IRQN_SPI1
CH32RV_SPI_SLAVE_HANDLER(1)
#endif
#ifdef CH32RV_IRQN_SPI2
CH32RV_SPI_SLAVE_HANDLER(2)
#endif
#ifdef CH32RV_IRQN_SPI3
CH32RV_SPI_SLAVE_HANDLER(3)
#endif
