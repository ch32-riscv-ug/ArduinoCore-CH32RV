/* CH32RVDfsdm.h. The registers are the STM32 DFSDM's with the two channels'
 * and the two filters' interleaved word by word (EVT ch32h417.h): channel y's
 * register r at 0x40017000 + 4 y + 8 r (CFGR1, CFGR2, AWSCDR, WDATR, DATINR),
 * filter x's register k at 0x40017028 + 4 x + 8 k (CR1, CR2, ISR, ICR, JCHGR,
 * FCR3, JDATAR, RDATAR, ...). CKOUT, its divider (CKOUTDIV: clock / (n + 1),
 * 0 none) and its source (CKOUTSRC 0: the EVT's "system clock", HCLK as
 * measured - 100 MHz on H417 at 400 MHz gave HCLK / 200 = 500 kHz,
 * 2026-11-23) and the DFSDM's enable are channel 0's CFGR1 (EVT
 * DFSDM_OutSerialClkConfig). */
#include "CH32RVDfsdm.h"

#if CH32RV_HAS_DFSDM

#include <stdlib.h>

#include "ch32rv_priority.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#define BASE 0x40017000u
#define CH(y, r) CH32RV_REG32(BASE + 4u * (y) + 8u * (r))
#define FLT(x, k) CH32RV_REG32(BASE + 0x28u + 4u * (x) + 8u * (k))
#define CFGR1(y) CH(y, 0)
#define CFGR2(y) CH(y, 1)
#define CR1(x) FLT(x, 0)
#define CR2(x) FLT(x, 1)
#define ISR(x) FLT(x, 2)
#define ICR(x) FLT(x, 3)
#define FCR3(x) FLT(x, 5)
#define RDATAR(x) FLT(x, 7)

#define CFGR1_SITP_FALLING (1u << 0)
#define CFGR1_SPICKSEL_CKOUT (1u << 2)
#define CFGR1_CHEN (1u << 7)
#define CFGR1_CHINSEL (1u << 8)
#define CFGR1_DFSDMEN (1u << 31)
#define CR1_DFEN (1u << 0)
#define CR1_RSWSTART (1u << 17)
#define CR1_RCONT (1u << 18)
#define CR1_RCH_SHIFT 24
#define CR1_FAST (1u << 29)
#define CR2_REOCIE (1u << 1)
#define ISR_REOCF (1u << 1)
#define ISR_ROVRF (1u << 3)
#define ICR_CLRROVRF (1u << 3)

#define RCC_HB2PRSTR CH32RV_REG32(0x40021010u)
#define RCC_HB2PCENR CH32RV_REG32(0x4002101Cu)
#define DFSDM_RCC_BIT (1u << 25)

namespace {
struct Pad {
    uint8_t role;   /* 0 CKIN0, 1 CKIN1, 2 DATIN0, 3 DATIN1, 4 CKOUT */
    uint8_t pad;
    uint32_t value, value2;
};
const Pad pads[] = CH32RV_DFSDM_PADS;
CH32RVDfsdm *owners[2];
uint8_t users;   /* bit x: filter x begun (the DFSDM and CKOUT stay on while either is) */

void on_irq(void *arg)
{
    static_cast<CH32RVDfsdm *>(arg)->onInterrupt();
}

const Pad *find(int pin, uint8_t role)
{
    for (const Pad &p : pads) {
        if (p.role == role && p.pad == (uint8_t)pin) {
            return &p;
        }
    }
    return nullptr;
}
}   // namespace

bool CH32RVDfsdm::pad(int pin, uint8_t role, bool output)
{
    const Pad *p = find(pin, role);
    if (!p) {
        return false;
    }
    ch32rv_pin_set_af(p->pad, p->value);
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin),
                           output ? CH32RV_GPIO_CFG_AF_PP_50M : CH32RV_GPIO_CFG_IN_FLOAT);
    return true;
}

bool CH32RVDfsdm::setBuffer(size_t samples)
{
    if (_begun || samples < 2) {
        return false;
    }
    _size = samples;
    return true;
}

bool CH32RVDfsdm::begin(int clockPin, int dataPin, uint32_t clockHz, uint16_t oversample, uint8_t order,
                        uint16_t integrator)
{
    end();
    if (_filter > 1 || owners[_filter] || clockHz == 0 || oversample < 1 || oversample > 1024 || order > 5 ||
        integrator < 1 || integrator > 256) {
        return false;
    }
    /* The pad's channel; when the other filter has it, the other channel
     * reads the same pads (CHINSEL: the next channel's pins) - two
     * microphones on one line, one on each edge. */
    const int8_t pins_of = find(dataPin, 2) ? 0 : find(dataPin, 3) ? 1 : -1;
    if (pins_of < 0) {
        return false;
    }
    const CH32RVDfsdm *other = owners[_filter ^ 1u];
    int8_t channel = pins_of;
    if (other && other->_channel == channel) {
        channel = (int8_t)(pins_of ^ 1);
    }
    if (other && other->_channel == channel) {
        return false;
    }
    const bool internal = find(clockPin, 4) != nullptr;
    if (!internal && !find(clockPin, (uint8_t)pins_of)) {
        return false;
    }
    /* The full scale: F ^ N x I (FastSinc 2 F ^ 2 x I), within the 32 bits
     * the filter computes in; shifted right to the 24 bits of RDATAR. */
    uint64_t full = order == 0 ? 2ull * oversample * oversample : 1ull;
    for (uint8_t k = 0; k < order; k++) {
        full *= oversample;
    }
    full *= integrator;
    if (full > 0x7FFFFFFFull) {
        return false;
    }
    uint8_t shift = 0;
    while ((full >> shift) > 0x7FFFFFull) {
        shift++;
    }
    /* CKOUT: HCLK / (div + 1), div 1 .. 255; one CKOUT for both filters. */
    uint32_t hz = clockHz;
    uint32_t div = 0;
    if (internal) {
        const uint32_t now = (CFGR1(0) >> 16) & 0xFFu;
        div = (CH32RV_HCLK_HZ + clockHz - 1u) / clockHz;
        div = div < 2u ? 1u : div > 256u ? 255u : div - 1u;
        if (users && now && now != div) {
            return false;   /* the other filter runs CKOUT at another rate */
        }
        hz = CH32RV_HCLK_HZ / (div + 1u);
    }
    int32_t *ring = static_cast<int32_t *>(malloc(_size * sizeof(int32_t)));
    if (!ring) {
        return false;
    }
    if (!users) {
        RCC_HB2PCENR |= DFSDM_RCC_BIT;
        RCC_HB2PRSTR |= DFSDM_RCC_BIT;
        RCC_HB2PRSTR &= ~DFSDM_RCC_BIT;
    }
    if (!pad(clockPin, internal ? 4 : (uint8_t)pins_of, internal) || !pad(dataPin, (uint8_t)(2 + pins_of), false)) {
        free(ring);
        return false;
    }
    _ring = ring;
    _head = _tail = 0;
    _overruns = 0;
    _channel = channel;
    _pins[0] = clockPin;
    _pins[1] = dataPin;
    _clock_hz = hz;
    _rate = hz / ((uint32_t)oversample * integrator);
    _full = (int32_t)(full >> shift);

    /* The channel: SPI data (SITP 0 rising / 1 falling), its clock from CKOUT
     * (SPICKSEL 1) or its CKIN pad (0), the right shift. These bits take a
     * write only while the whole DFSDM is off (DFSDMEN 0; with it on, CHEN
     * changed and they stayed 0 - H417, 2026-11-23): with the other filter
     * running, it stops for this and starts again after. */
    const bool restart = users != 0;
    CFGR1(0) &= ~CFGR1_DFSDMEN;
    CFGR1(channel) &= ~(CFGR1_CHEN | CFGR1_CHINSEL | 0xFu);
    CFGR2(channel) = (uint32_t)shift << 3;
    CFGR1(channel) |= (_falling ? CFGR1_SITP_FALLING : 0u) | (internal ? CFGR1_SPICKSEL_CKOUT : 0u) |
                      (channel != pins_of ? CFGR1_CHINSEL : 0u);
    if (internal) {
        CFGR1(0) = (CFGR1(0) & ~(0xFFu << 16 | 1u << 30)) | div << 16;
    }
    /* The filter: sinc order, FOSR - 1, IOSR - 1; continuous regular
     * conversions of the channel, fast mode (no settling between them). */
    CR1(_filter) = 0;
    FCR3(_filter) = (uint32_t)order << 29 | (uint32_t)(oversample - 1u) << 16 | (uint32_t)(integrator - 1u);
    CR2(_filter) = CR2_REOCIE;
    CR1(_filter) = (uint32_t)channel << CR1_RCH_SHIFT | CR1_RCONT | CR1_FAST;
    CR1(_filter) |= CR1_DFEN;
    CFGR1(channel) |= CFGR1_CHEN;
    CFGR1(0) |= CFGR1_DFSDMEN;
    if (restart) {
        CR1(_filter ^ 1u) |= CR1_RSWSTART;
    }
    owners[_filter] = this;
    users |= (uint8_t)(1u << _filter);
    _begun = true;
    if (!ch32rv_irq_attach(_filter ? CH32RV_IRQN_DFSDM1 : CH32RV_IRQN_DFSDM0, on_irq, this)) {
        end();
        return false;
    }
    CR1(_filter) |= CR1_RSWSTART;
    return true;
}

void CH32RVDfsdm::onInterrupt()
{
    const uint32_t isr = ISR(_filter);
    if (isr & ISR_ROVRF) {
        ICR(_filter) = ICR_CLRROVRF;
        _overruns++;
    }
    if (isr & ISR_REOCF) {
        const int32_t v = (int32_t)RDATAR(_filter) >> 8;   /* reading clears REOCF */
        const size_t next = (_head + 1u) % _size;
        if (next == _tail) {
            _overruns++;
        } else {
            _ring[_head] = v;
            _head = next;
        }
    }
}

void CH32RVDfsdm::end()
{
    if (!_begun) {
        return;
    }
    ch32rv_irq_detach(_filter ? CH32RV_IRQN_DFSDM1 : CH32RV_IRQN_DFSDM0);
    CR1(_filter) = 0;
    CR2(_filter) = 0;
    CFGR1(_channel) &= ~CFGR1_CHEN;
    owners[_filter] = nullptr;
    users &= (uint8_t)~(1u << _filter);
    if (!users) {
        CFGR1(0) &= ~(CFGR1_DFSDMEN | 0xFFu << 16);
        RCC_HB2PCENR &= ~DFSDM_RCC_BIT;
    }
    const CH32RVDfsdm *other = owners[_filter ^ 1u];
    for (int pin : _pins) {
        if (pin >= 0 && !(other && (other->_pins[0] == pin || other->_pins[1] == pin))) {
            ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                                   CH32RV_GPIO_CFG_IN_FLOAT);
        }
    }
    _pins[0] = _pins[1] = -1;
    _channel = -1;
    _begun = false;
    free(_ring);
    _ring = nullptr;
}

int CH32RVDfsdm::available() const
{
    if (!_begun) {
        return 0;
    }
    return (int)((_head + _size - _tail) % _size);
}

int32_t CH32RVDfsdm::read()
{
    if (!_begun) {
        return 0;
    }
    while (_head == _tail) {
    }
    const int32_t v = _ring[_tail];
    _tail = (_tail + 1u) % _size;
    return v;
}

size_t CH32RVDfsdm::read(int32_t *samples, size_t n)
{
    size_t k = 0;
    while (_begun && k < n && _head != _tail) {
        samples[k++] = _ring[_tail];
        _tail = (_tail + 1u) % _size;
    }
    return k;
}

#else

bool CH32RVDfsdm::setBuffer(size_t) { return false; }
bool CH32RVDfsdm::begin(int, int, uint32_t, uint16_t, uint8_t, uint16_t) { return false; }
void CH32RVDfsdm::end() {}
int CH32RVDfsdm::available() const { return 0; }
int32_t CH32RVDfsdm::read() { return 0; }
size_t CH32RVDfsdm::read(int32_t *, size_t) { return 0; }
void CH32RVDfsdm::onInterrupt() {}

#endif
