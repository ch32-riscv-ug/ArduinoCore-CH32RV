#include "SPI.h"
#include "ch32rv_spi_claim.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_dma.h"

using namespace arduino;

/* SPI1 counts PCLK2 and the others PCLK1. They are the same clock except
 * where the CPU Clock entry halves APB2 (Arduino.h's CH32RV_PCLK2_HZ); there
 * the instance picks, elsewhere this is a constant. */
static inline uint32_t spi_pclk(uint32_t base)
{
#if CH32RV_PCLK1_HZ == CH32RV_PCLK2_HZ
    (void)base;
    return CH32RV_PCLK1_HZ;
#else
    return base == CH32RV_SPI1_BASE ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
#endif
}

/* BR selects a power-of-two divider from 2 to 256. Pick the first one that
 * does not exceed the requested clock, so asking for 1 MHz on a 48 MHz part
 * gives 750 kHz rather than 1.5 MHz - going over is what breaks a device with
 * a maximum rating. */
static uint16_t br_for(uint32_t pclk, uint32_t clock_hz)
{
    if (clock_hz == 0) {
        return 7u;                                  /* slowest */
    }
    for (uint16_t br = 0; br < 7u; br++) {
        if (pclk >> (br + 1) <= clock_hz) {
            return br;
        }
    }
    return 7u;
}

/* Which SPIs are in the table, and the board's choice for each (Disable /
 * AUTO / FIX): ch32rv_instances.h, through SPI.h. */
#define CH32RV_SPI_DISABLE CH32RV_PERIPH_DISABLE
#define CH32RV_SPI_AUTO    CH32RV_PERIPH_AUTO
#define CH32RV_SPI_FIX     CH32RV_PERIPH_FIX
#if defined(CH32RV_SPI1_CLKEN_ADDR) && !defined(CH32RV_SPI1_REMAP_MASK)
#define CH32RV_SPI1_REMAP_MASK 0u
#define CH32RV_SPI1_REMAP_VAL  0u
#endif
#if defined(CH32RV_SPI1_CLKEN_ADDR) && !defined(CH32RV_SPI1_REMAP2_MASK)
#define CH32RV_SPI1_REMAP2_MASK 0u
#define CH32RV_SPI1_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SPI2_CLKEN_ADDR) && !defined(CH32RV_SPI2_REMAP_MASK)
#define CH32RV_SPI2_REMAP_MASK 0u
#define CH32RV_SPI2_REMAP_VAL  0u
#endif
#if defined(CH32RV_SPI2_CLKEN_ADDR) && !defined(CH32RV_SPI2_REMAP2_MASK)
#define CH32RV_SPI2_REMAP2_MASK 0u
#define CH32RV_SPI2_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SPI3_CLKEN_ADDR) && !defined(CH32RV_SPI3_REMAP_MASK)
#define CH32RV_SPI3_REMAP_MASK 0u
#define CH32RV_SPI3_REMAP_VAL  0u
#endif
#if defined(CH32RV_SPI3_CLKEN_ADDR) && !defined(CH32RV_SPI3_REMAP2_MASK)
#define CH32RV_SPI3_REMAP2_MASK 0u
#define CH32RV_SPI3_REMAP2_VAL  0u
#endif

namespace {

struct SpiDesc {
    uint32_t base;
    uint32_t clken_addr;
    uint32_t clken_mask;
    uint32_t remap_mask;
    uint32_t remap2_mask;
    ch32rv_route_t def;      /* the route a begin() without pins uses */
    uint8_t number;          /* n of SPIn */
};

/* Every route an SPI can take, for setPins() / setRoute() only. */
struct RouteSet {
    const ch32rv_route_t *rows;
    uint8_t count;
};

#define CH32RV_SPI_ROUTE5(sck, miso, mosi, v1, v2) { 0, { sck, miso, mosi }, v1, v2 }
#define CH32RV_SPI_ROUTE(...) CH32RV_SPI_ROUTE5(__VA_ARGS__)
/* An SPI the variant names no default route for (its pins move between the
 * packages of the series): begin() without pins fails on it. */
#define CH32RV_SPI_NO_DEF CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, 0u, 0u

#if CH32RV_SPI_HW_1
#define CH32RV_SPI_HAS_1 1
#if CH32RV_SPI1_MODE == CH32RV_SPI_FIX
#define CH32RV_SPI_DEF_1 CH32RV_SPI1_FIX_SCK, CH32RV_SPI1_FIX_MISO, CH32RV_SPI1_FIX_MOSI, CH32RV_SPI1_FIX_V1, CH32RV_SPI1_FIX_V2
static const ch32rv_route_t spi1_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_1) };
#else
#if defined(CH32RV_SPI1_SCK)
#define CH32RV_SPI_DEF_1 CH32RV_SPI1_SCK, CH32RV_SPI1_MISO, CH32RV_SPI1_MOSI, CH32RV_SPI1_REMAP_VAL, CH32RV_SPI1_REMAP2_VAL
#else
#define CH32RV_SPI_DEF_1 CH32RV_SPI_NO_DEF
#endif
#if defined(CH32RV_SPI1_ROUTES)
static const ch32rv_route_t spi1_routes[] = CH32RV_SPI1_ROUTES;
#else
static const ch32rv_route_t spi1_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_1) };
#endif
#endif
#endif
#if CH32RV_SPI_HW_2
#define CH32RV_SPI_HAS_2 1
#if CH32RV_SPI2_MODE == CH32RV_SPI_FIX
#define CH32RV_SPI_DEF_2 CH32RV_SPI2_FIX_SCK, CH32RV_SPI2_FIX_MISO, CH32RV_SPI2_FIX_MOSI, CH32RV_SPI2_FIX_V1, CH32RV_SPI2_FIX_V2
static const ch32rv_route_t spi2_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_2) };
#else
#if defined(CH32RV_SPI2_SCK)
#define CH32RV_SPI_DEF_2 CH32RV_SPI2_SCK, CH32RV_SPI2_MISO, CH32RV_SPI2_MOSI, CH32RV_SPI2_REMAP_VAL, CH32RV_SPI2_REMAP2_VAL
#else
#define CH32RV_SPI_DEF_2 CH32RV_SPI_NO_DEF
#endif
#if defined(CH32RV_SPI2_ROUTES)
static const ch32rv_route_t spi2_routes[] = CH32RV_SPI2_ROUTES;
#else
static const ch32rv_route_t spi2_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_2) };
#endif
#endif
#endif
#if CH32RV_SPI_HW_3
#define CH32RV_SPI_HAS_3 1
#if CH32RV_SPI3_MODE == CH32RV_SPI_FIX
#define CH32RV_SPI_DEF_3 CH32RV_SPI3_FIX_SCK, CH32RV_SPI3_FIX_MISO, CH32RV_SPI3_FIX_MOSI, CH32RV_SPI3_FIX_V1, CH32RV_SPI3_FIX_V2
static const ch32rv_route_t spi3_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_3) };
#else
#if defined(CH32RV_SPI3_SCK)
#define CH32RV_SPI_DEF_3 CH32RV_SPI3_SCK, CH32RV_SPI3_MISO, CH32RV_SPI3_MOSI, CH32RV_SPI3_REMAP_VAL, CH32RV_SPI3_REMAP2_VAL
#else
#define CH32RV_SPI_DEF_3 CH32RV_SPI_NO_DEF
#endif
#if defined(CH32RV_SPI3_ROUTES)
static const ch32rv_route_t spi3_routes[] = CH32RV_SPI3_ROUTES;
#else
static const ch32rv_route_t spi3_routes[] = { CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_3) };
#endif
#endif
#endif

#define CH32RV_SPI_DESC(n)                                                             \
    { CH32RV_SPI##n##_BASE, CH32RV_SPI##n##_CLKEN_ADDR, CH32RV_SPI##n##_CLKEN_MASK,     \
      CH32RV_SPI##n##_REMAP_MASK, CH32RV_SPI##n##_REMAP2_MASK,                          \
      CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_##n), n },
#define CH32RV_SPI_SET(n) \
    { spi##n##_routes, (uint8_t)(sizeof spi##n##_routes / sizeof spi##n##_routes[0]) },

#if defined(CH32RV_SPI_HAS_1) || defined(CH32RV_SPI_HAS_2) || defined(CH32RV_SPI_HAS_3)
const SpiDesc spis[] = {
#ifdef CH32RV_SPI_HAS_1
    CH32RV_SPI_DESC(1)
#endif
#ifdef CH32RV_SPI_HAS_2
    CH32RV_SPI_DESC(2)
#endif
#ifdef CH32RV_SPI_HAS_3
    CH32RV_SPI_DESC(3)
#endif
};
const RouteSet route_sets[] = {
#ifdef CH32RV_SPI_HAS_1
    CH32RV_SPI_SET(1)
#endif
#ifdef CH32RV_SPI_HAS_2
    CH32RV_SPI_SET(2)
#endif
#ifdef CH32RV_SPI_HAS_3
    CH32RV_SPI_SET(3)
#endif
};
constexpr uint8_t SPI_COUNT = (uint8_t)(sizeof spis / sizeof spis[0]);
#else
/* Every SPI is Disable on this board: buses exist, begin() never opens one. */
const SpiDesc spis[1] = {};
const RouteSet route_sets[1] = {};
constexpr uint8_t SPI_COUNT = 0;
#endif

/* Each SPI's DMA requests, in the table's order (the variant's
 * CH32RV_SPIn_DMA_RX / _TX). Only the asynchronous transfer reads it. */
struct SpiDma {
    ch32rv_dma_line_t rx;
    ch32rv_dma_line_t tx;
};
#define CH32RV_SPI_NO_DMA { CH32RV_DMA_LINE(0, 0, 0u, 0u, 0u, 0u), CH32RV_DMA_LINE(0, 0, 0u, 0u, 0u, 0u) },
#if defined(CH32RV_SPI_HAS_1) || defined(CH32RV_SPI_HAS_2) || defined(CH32RV_SPI_HAS_3)
const SpiDma spi_dma[] = {
#ifdef CH32RV_SPI_HAS_1
#ifdef CH32RV_SPI1_DMA_TX
    { CH32RV_SPI1_DMA_RX, CH32RV_SPI1_DMA_TX },
#else
    CH32RV_SPI_NO_DMA
#endif
#endif
#ifdef CH32RV_SPI_HAS_2
#ifdef CH32RV_SPI2_DMA_TX
    { CH32RV_SPI2_DMA_RX, CH32RV_SPI2_DMA_TX },
#else
    CH32RV_SPI_NO_DMA
#endif
#endif
#ifdef CH32RV_SPI_HAS_3
#ifdef CH32RV_SPI3_DMA_TX
    { CH32RV_SPI3_DMA_RX, CH32RV_SPI3_DMA_TX },
#else
    CH32RV_SPI_NO_DMA
#endif
#endif
};
#else
const SpiDma spi_dma[1] = { CH32RV_SPI_NO_DMA };
#endif

/* A board with one enabled SPI has one bus object (SPI) and one instance, so
 * there is nothing to share and nobody else to own it: no owner table, no
 * search. */
#if CH32RV_SPI_HW_COUNT == 1
#define CH32RV_SPI_SINGLE 1
#else
#define CH32RV_SPI_SINGLE 0
#endif
#if !CH32RV_SPI_SINGLE
/* Which bus (or CH32RVSPISlave) holds each SPI. */
const void *owner[SPI_COUNT > 0 ? SPI_COUNT : 1];
#endif

inline uint8_t index_of(uint8_t slot)
{
    return SPI_COUNT == 1 ? 0 : (uint8_t)(slot - 1);
}

struct Found {
    int index;
    const ch32rv_route_t *row;
};

/* A free SPI with a route on these pins; this bus's own SPI first. */
Found find_route(uint8_t prefer, const uint8_t *want, const void *self)
{
    Found first = {-1, nullptr};
    for (uint8_t i = 0; i < SPI_COUNT; i++) {
#if !CH32RV_SPI_SINGLE
        if (owner[i] && owner[i] != self) {
            continue;
        }
#else
        (void)self;
#endif
        const RouteSet &set = route_sets[i];
        for (uint8_t r = 0; r < set.count; r++) {
            const uint8_t *p = set.rows[r].pins;
            if (p[0] == want[0] && p[1] == want[1] && p[2] == want[2]) {
                if (i == prefer) {
                    return {(int)i, &set.rows[r]};
                }
                if (first.index < 0) {
                    first = {(int)i, &set.rows[r]};
                }
            }
        }
    }
    return first;
}

void release_pin(uint8_t pin)
{
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_IN_FLOAT);
}

}  // namespace

/* For SPIHwCs.cpp: the bus's route value (the pads' alternate function where
 * chosen per pin) - the route begin() took. */
uint32_t CH32RVSPIClass::route_value(void) const
{
    if (!_slot) {
        return 0;
    }
    if (_want) {
        return _want->value;
    }
    return spis[index_of(_slot)].def.value;
}

uint8_t CH32RVSPIClass::slot_index(void) const
{
    return index_of(_slot);
}

void (*ch32rv_spi_hwcs)(arduino::CH32RVSPIClass &bus);

uint8_t CH32RVSPIClass::peripheral(void) const
{
    return _slot ? spis[index_of(_slot)].number : 0;
}

void CH32RVSPIClass::begin()
{
    if (_slot) {
        end();
    }
#if CH32RV_SPI_SINGLE
    if (SPI_COUNT == 0) {
        return;                /* the SPI is Disable on this board */
    }
    /* Its own constant rather than &spis[0].def: a pointer into the table
     * would keep the whole table in flash for 12 bytes of it. */
#if defined(CH32RV_SPI_HAS_1)
    static const ch32rv_route_t def = CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_1);
#elif defined(CH32RV_SPI_HAS_2)
    static const ch32rv_route_t def = CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_2);
#else
    static const ch32rv_route_t def = CH32RV_SPI_ROUTE(CH32RV_SPI_DEF_3);
#endif
    const uint8_t index = 0;
    if (!_want && def.pins[0] == CH32RV_ROUTE_NO_PIN) {
        return;                /* no default route: pins have to be named */
    }
    const ch32rv_route_t *route = _want ? _want : &def;
#else
    uint8_t index = _want_index;
    const ch32rv_route_t *route = _want;
    if (!route) {
        /* No pins named: the fallback, this name's row on its default (or
         * fixed) pins. */
        if (SPI_COUNT == 0) {
            return;            /* every SPI is Disable on this board */
        }
        index = _number;
        route = &spis[index].def;
        if (route->pins[0] == CH32RV_ROUTE_NO_PIN) {
            return;            /* no default route: pins have to be named */
        }
    }
    if (SPI_COUNT == 1) {
        index = 0;             /* the only instance: lets the table fold to constants */
    }
    if (owner[index] && owner[index] != this) {
        return;
    }
#endif
    const SpiDesc &d = spis[index];
#if !CH32RV_SPI_SINGLE
    owner[index] = this;
#endif
    _slot = (uint8_t)(index + 1);
    _base = d.base;
    _sck_pin = route->pins[0];
    _miso_pin = route->pins[1];
    _mosi_pin = route->pins[2];

    ch32rv_clock_enable_at(d.clken_addr, d.clken_mask);
    ch32rv_clock_enable(AFIO);
    if (d.remap_mask) {
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~d.remap_mask) | route->value;
    }
    if (d.remap2_mask) {
        CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~d.remap2_mask) | route->value2;
    }
    ch32rv_pin_set_af(_sck_pin, route->value);
    ch32rv_pin_set_af(_miso_pin, route->value);
    ch32rv_pin_set_af(_mosi_pin, route->value);

    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(_sck_pin));
    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(_miso_pin));
    ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(_mosi_pin));
    pads(false);

    _periph = 0;
    _started = true;
    apply(_clock_hz, _order, _mode);
}

/* The pads as a controller drives them, or as a peripheral: SCK and MOSI
 * come from the other end, MISO is this one's. */
void CH32RVSPIClass::pads(bool peripheral)
{
    const uint32_t out = peripheral ? CH32RV_GPIO_CFG_IN_FLOAT : CH32RV_GPIO_CFG_AF_PP_50M;
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(_sck_pin), (uint8_t)CH32RV_PIN_BIT(_sck_pin), out);
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(_mosi_pin), (uint8_t)CH32RV_PIN_BIT(_mosi_pin), out);
    /* A controller's MISO is the device's, so an input. Pulled up rather than
     * floating: with nothing connected a floating input picks up noise and
     * reads back as random bytes instead of a steady 0xFF, which makes "no
     * device" look like "flaky device". */
    const uint8_t miso_port = (uint8_t)CH32RV_PIN_PORT(_miso_pin);
    const uint8_t miso_bit = (uint8_t)CH32RV_PIN_BIT(_miso_pin);
    ch32rv_gpio_set_config(miso_port, miso_bit,
                           peripheral ? CH32RV_GPIO_CFG_AF_PP_50M : CH32RV_GPIO_CFG_IN_PULL);
    if (!peripheral) {
        ch32rv_gpio_set(miso_port, miso_bit);
    }
}

/* Into or out of peripheral mode, the SPI reset through RCC on the way (PRSTR,
 * 0x0C below the clock enable register, every SPI of every family - as
 * CH32RVSPISlave does): a slave's transmit side may hold a byte the controller
 * never clocked, which a controller would send first. */
void CH32RVSPIClass::peripheral_mode(bool on)
{
    const SpiDesc &d = spis[index_of(_slot)];
    CH32RV_SPI_CTLR1(_base) = 0;
    CH32RV_REG32(d.clken_addr - 0x0Cu) |= d.clken_mask;
    CH32RV_REG32(d.clken_addr - 0x0Cu) &= ~d.clken_mask;
    pads(on);
    if (!on) {
        _periph = 0;
        apply(_clock_hz, _order, _mode);
        return;
    }
    /* Slave, NSS from SSM with SSI 0: selected until endTransaction(). */
    uint16_t ctlr1 = CH32RV_SPI_CTLR1_SSM;
    if (_mode & 0x1u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPHA;
    }
    if (_mode & 0x2u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPOL;
    }
    CH32RV_SPI_CTLR1(_base) = ctlr1;
    CH32RV_SPI_CTLR1(_base) = (uint16_t)(ctlr1 | CH32RV_SPI_CTLR1_SPE);
    _periph = _order == LSBFIRST ? 2 : 1;
}





void CH32RVSPIClass::end()
{
    if (!_slot) {
        _started = false;
        return;
    }
    const SpiDesc &d = spis[index_of(_slot)];
    CH32RV_SPI_CTLR1(_base) = 0;
    ch32rv_clock_disable_at(d.clken_addr, d.clken_mask);
#if !CH32RV_SPI_SINGLE
    owner[index_of(_slot)] = nullptr;
#endif
    _slot = 0;
    _started = false;
}

void CH32RVSPIClass::apply(uint32_t clock_hz, BitOrder order, uint8_t mode)
{
    _clock_hz = clock_hz;
    _order = order;
    _mode = mode;
    if (!_started) {
        return;                       /* begin() applies it */
    }
    if (_periph == 3) {               /* let go by a peripheral transaction: the pads back */
        pads(false);
        _periph = 0;
    }

    /* SSM/SSI keep the peripheral in master mode without owning an NSS pin:
     * without them a low NSS input drops it out of master mode (MODF) and the
     * transfer silently never happens. */
    uint16_t ctlr1 = CH32RV_SPI_CTLR1_MSTR |
                     ((ch32rv_spi_hwcs && _hwcs) ? 0u : (uint16_t)(CH32RV_SPI_CTLR1_SSM | CH32RV_SPI_CTLR1_SSI)) |
                     (uint16_t)(br_for(spi_pclk(_base), clock_hz) << CH32RV_SPI_CTLR1_BR_SHIFT);
    if (mode & 0x1u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPHA;
    }
    if (mode & 0x2u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPOL;
    }
    if (order == LSBFIRST) {
        ctlr1 |= CH32RV_SPI_CTLR1_LSBFIRST;
    }

    /* beginTransaction() runs this before every transfer, usually with the
     * settings the bus already has: then nothing is touched. */
    const uint16_t now = CH32RV_SPI_CTLR1(_base);
    if (now == (uint16_t)(ctlr1 | CH32RV_SPI_CTLR1_SPE)) {
        return;
    }
    /* SPE off while the settings change, keeping MSTR and the old CPOL: a
     * CTLR1 of 0 makes the peripheral a slave with CPOL 0 for that moment,
     * and SCK left its idle level - a pulse a device could take for a clock. */
    CH32RV_SPI_CTLR1(_base) = (uint16_t)(now & ~CH32RV_SPI_CTLR1_SPE);
    /* setHwCs(): NSS an output, low while SPE is on (SSOE) */
    CH32RV_SPI_CTLR2(_base) = (ch32rv_spi_hwcs && _hwcs) ? CH32RV_SPI_CTLR2_SSOE : 0u;
    CH32RV_SPI_CTLR1(_base) = ctlr1;
    CH32RV_SPI_CTLR1(_base) = (uint16_t)(ctlr1 | CH32RV_SPI_CTLR1_SPE);
}

void CH32RVSPIClass::beginTransaction(SPISettings settings)
{
    if (settings.getBusMode() != SPI_CONTROLLER) {
        if (_started && ch32rv_spi_peripheral) {
            _order = settings.getBitOrder();
            _mode = (uint8_t)settings.getDataMode();
            ch32rv_spi_peripheral->hw_mode(*this, true);
        }
        return;
    }
    if (_periph) {
        ch32rv_spi_peripheral->hw_mode(*this, false);
    }
    apply(settings.getClockFreq(), settings.getBitOrder(),
          (uint8_t)settings.getDataMode());
}

void CH32RVSPIClass::endTransaction(void)
{
    /* A controller's bus is left configured and enabled. Arduino's contract
     * is only that another device may now claim it, and re-applying settings
     * is what beginTransaction does. A peripheral lets the bus go: the SPI
     * off and its pads left as inputs, so it never drives SCK / MOSI against
     * the controller that clocked it (it did, at its last mode's CPOL, as soon
     * as it became a controller again - the controller's next mode met it:
     * 25 ns SCK pulses and a 33rd clock, V006 / V203, 2026-11-15). The next
     * controller transfer or beginTransaction() takes the pads back. */
    if (_periph == 1 || _periph == 2) {
        CH32RV_SPI_CTLR1(_base) = 0;
        _periph = 3;
    } else if (ch32rv_spi_hwcs && _hwcs && _started && !_periph) {
        ch32rv_spi_hwcs(*this);      /* setHwCs(): NSS high (SPIHwCs.cpp) */
    }
}

void CH32RVSPIClass::setBitOrder(BitOrder order)
{
    apply(_clock_hz, order, _mode);
}

void CH32RVSPIClass::setDataMode(uint8_t mode)
{
    apply(_clock_hz, _order, (uint8_t)(mode & 0x3u));
}

void CH32RVSPIClass::setClockDivider(uint32_t divider)
{
    apply(divider ? CH32RV_HCLK_HZ / divider : CH32RV_HCLK_HZ / 256u,
          _order, _mode);
}

uint8_t CH32RVSPIClass::transfer(uint8_t data)
{
    if (!_started) {
        return 0;
    }
    if (_periph == 3) {
        apply(_clock_hz, _order, _mode);   /* let go after a peripheral transaction: a controller again */
    } else if (_periph) {
        return ch32rv_spi_peripheral->hw_transfer(*this, data);
    }
    /* Full duplex: one byte out is one byte in, and the wait for RXNE is what
     * makes the call synchronous. No timeout, because unlike I2C there is no
     * other party that can stall the clock - the master generates it. */
    while ((CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_TXE) == 0u) {
    }
    CH32RV_SPI_DATAR(_base) = data;
    while ((CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_RXNE) == 0u) {
    }
    return (uint8_t)CH32RV_SPI_DATAR(_base);
}

void CH32RVSPIClass::transfer_block(const uint8_t *tx, uint8_t *rx, size_t count)
{
    if (!_started || count == 0) {
        return;
    }
    if (_periph) {
        /* a peripheral, or let go by one: transfer() knows both */
        ::SPIClass::transfer_block(tx, rx, count);
        return;
    }
    /* One byte at a time. Out only (rx null), the next byte goes in as soon
     * as the data register is free and the bytes leave back to back; what
     * comes in is dropped, and the overrun that leaves is cleared at the end.
     * Both ways, a byte is read before the next goes, so an interrupt between
     * them never makes the receive side overrun. */
    const uint32_t base = _base;
    for (size_t i = 0; i < count; i++) {
        while ((CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_TXE) == 0u) {
        }
        CH32RV_SPI_DATAR(base) = tx ? tx[i] : 0xFFu;
        if (rx) {
            while ((CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_RXNE) == 0u) {
            }
            rx[i] = (uint8_t)CH32RV_SPI_DATAR(base);
        }
    }
    if (!rx) {
        while ((CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_TXE) == 0u) {
        }
        while (CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_BSY) {
        }
        (void)CH32RV_SPI_DATAR(base);
        (void)CH32RV_SPI_STATR(base);
    }
}

/* ------------------------------------------------------ asynchronous */
/* A transfer moved by DMA while the sketch runs on (transferAsync()): the
 * transmit channel feeds the data register, the receive one (when recv is
 * wanted) empties it. The channels are this SPI's fixed requests (on H41x the
 * DMAMUX channels the variant chose), held from transferAsync() until
 * finishedAsync() sees the end: another user holding one (a HardwareSerial DMA
 * buffer on the same channel) makes transferAsync() false. A channel counts
 * 65535 at most, so a longer one runs in parts, the next started by the
 * finishedAsync() that sees the one before end. The state is here, not in the
 * bus object, so a sketch that never calls transferAsync() carries none of
 * it. */
namespace {
struct AsyncRun {
    const uint8_t *tx;      /* null: 0xFF out */
    uint8_t *rx;            /* null: what comes in is dropped */
    size_t left;            /* not yet started */
    uint16_t part;          /* the part running */
    bool busy;
};
AsyncRun async_runs[SPI_COUNT > 0 ? SPI_COUNT : 1];
const uint8_t async_ones = 0xFFu;

void async_start(uint32_t base, const SpiDma &d, AsyncRun &run)
{
    const uint32_t n = run.left > 0xFFFFu ? 0xFFFFu : (uint32_t)run.left;
    const uint32_t data = (uint32_t)(uintptr_t)&CH32RV_SPI_DATAR(base);
    run.part = (uint16_t)n;
    run.left -= n;
    CH32RV_SPI_CTLR2(base) = 0;
    if (run.rx) {
        ch32rv_dma_close(&d.rx);
        CH32RV_DMA_PADDR(&d.rx) = data;
        CH32RV_DMA_MADDR(&d.rx) = (uint32_t)(uintptr_t)run.rx;
        CH32RV_DMA_CNTR(&d.rx) = n;
        CH32RV_DMA_CFGR(&d.rx) = CH32RV_DMA_CFGR_MINC | CH32RV_DMA_CFGR_PL(3) | CH32RV_DMA_CFGR_EN;
    }
    ch32rv_dma_close(&d.tx);
    CH32RV_DMA_PADDR(&d.tx) = data;
    CH32RV_DMA_MADDR(&d.tx) = (uint32_t)(uintptr_t)(run.tx ? run.tx : &async_ones);
    CH32RV_DMA_CNTR(&d.tx) = n;
    CH32RV_DMA_CFGR(&d.tx) = (run.tx ? CH32RV_DMA_CFGR_MINC : 0u) | CH32RV_DMA_CFGR_DIR |
                             CH32RV_DMA_CFGR_PL(2) | CH32RV_DMA_CFGR_EN;
    CH32RV_SPI_CTLR2(base) = (uint16_t)((run.rx ? CH32RV_SPI_CTLR2_RXDMAEN : 0u) | CH32RV_SPI_CTLR2_TXDMAEN);
}

/* The bus idle and nothing left in its receive side: out only leaves an
 * overrun behind. */
void async_settle(uint32_t base)
{
    while ((CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_TXE) == 0u) {
    }
    while (CH32RV_SPI_STATR(base) & CH32RV_SPI_STATR_BSY) {
    }
    CH32RV_SPI_CTLR2(base) = 0;
    (void)CH32RV_SPI_DATAR(base);
    (void)CH32RV_SPI_STATR(base);
}

void async_release(const SpiDma &d, AsyncRun &run)
{
    ch32rv_dma_close(&d.tx);
    ch32rv_dma_release(d.tx.dma, d.tx.channel);
    if (run.rx) {
        ch32rv_dma_close(&d.rx);
        ch32rv_dma_release(d.rx.dma, d.rx.channel);
    }
    run.busy = false;
}
}  // namespace

bool CH32RVSPIClass::transferAsync(const void *send, void *recv, size_t bytes)
{
    if (!_started || _periph || SPI_COUNT == 0) {
        return false;
    }
    const uint8_t index = index_of(_slot);
    const SpiDma &d = spi_dma[index];
    AsyncRun &run = async_runs[index];
    if (d.tx.ch == 0u || run.busy) {
        return false;
    }
    if (bytes == 0) {
        return true;
    }
    if (!ch32rv_dma_claim(d.tx.dma, d.tx.channel)) {
        return false;
    }
    if (recv && !ch32rv_dma_claim(d.rx.dma, d.rx.channel)) {
        ch32rv_dma_release(d.tx.dma, d.tx.channel);
        return false;
    }
    ch32rv_dma_open(&d.tx);
    if (recv) {
        ch32rv_dma_open(&d.rx);
    }
    (void)CH32RV_SPI_DATAR(_base);             /* nothing left from before */
    (void)CH32RV_SPI_STATR(_base);
    run.tx = (const uint8_t *)send;
    run.rx = (uint8_t *)recv;
    run.left = bytes;
    run.busy = true;
    async_start(_base, d, run);
    return true;
}

bool CH32RVSPIClass::finishedAsync(void)
{
    if (SPI_COUNT == 0 || !_slot) {
        return true;
    }
    const uint8_t index = index_of(_slot);
    const SpiDma &d = spi_dma[index];
    AsyncRun &run = async_runs[index];
    if (!run.busy) {
        return true;
    }
    /* The part is over when its last byte is in (both ways) or handed to the
     * shift register (out only). A transfer error stops the channel: that
     * ends it too, rather than never. */
    const ch32rv_dma_line_t &last = run.rx ? d.rx : d.tx;
    const uint32_t flags = (CH32RV_DMA_INTFR(&last) >> last.shift) & (CH32RV_DMA_TCIF | CH32RV_DMA_TEIF);
    if (flags == 0u) {
        return false;
    }
    if (!run.rx) {
        async_settle(_base);
    }
    if (run.left && !(flags & CH32RV_DMA_TEIF)) {
        if (run.tx) {
            run.tx += run.part;
        }
        if (run.rx) {
            run.rx += run.part;
        }
        async_start(_base, d, run);
        return false;
    }
    CH32RV_SPI_CTLR2(_base) = 0;
    async_release(d, run);
    return true;
}

void CH32RVSPIClass::abortAsync(void)
{
    if (SPI_COUNT == 0 || !_slot) {
        return;
    }
    const uint8_t index = index_of(_slot);
    const SpiDma &d = spi_dma[index];
    AsyncRun &run = async_runs[index];
    if (!run.busy) {
        return;
    }
    /* The channels stopped first: the byte in the shift register finishes,
     * nothing after it starts. */
    CH32RV_DMA_CFGR(&d.tx) = 0;
    if (run.rx) {
        CH32RV_DMA_CFGR(&d.rx) = 0;
    }
    async_settle(_base);
    async_release(d, run);
}

static uint8_t reverse(uint8_t b)
{
    b = (uint8_t)((b & 0xF0u) >> 4 | (b & 0x0Fu) << 4);
    b = (uint8_t)((b & 0xCCu) >> 2 | (b & 0x33u) << 2);
    return (uint8_t)((b & 0xAAu) >> 1 | (b & 0x55u) << 1);
}

/* A peripheral's byte: the same exchange, the clock the controller's. It waits
 * as long as the controller takes (ArduinoCore-API's peripheral mode has no
 * timeout), and the byte has to be in before the controller clocks it: between
 * bytes the controller leaves the time from one transfer() to the next. LSB
 * first is reversed here, not by the SPI, whose LSB-first slave sends the
 * wrong bits on some parts (CH32RVSPISlave.cpp). */
uint8_t CH32RVSPIClass::peripheral_transfer(uint8_t data)
{
    const bool lsb = _periph == 2;
    if (lsb) {
        data = reverse(data);
    }
    while ((CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_TXE) == 0u) {
    }
    CH32RV_SPI_DATAR(_base) = data;
    while ((CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_RXNE) == 0u) {
    }
    const uint8_t in = (uint8_t)CH32RV_SPI_DATAR(_base);
    return lsb ? reverse(in) : in;
}



/* --------------------------------------------------------------- routes */
bool CH32RVSPIClass::setRoute(uint8_t route)
{
    if (SPI_COUNT == 0) {
        return false;
    }
    const RouteSet &set = route_sets[SPI_COUNT == 1 ? 0 : _number];
    const int r = ch32rv_route_find(set.rows, set.count, route);
    if (r < 0) {
        return false;
    }
    return setPins(set.rows[r].pins[0], set.rows[r].pins[1], set.rows[r].pins[2]);
}

bool CH32RVSPIClass::setPins(int sck, int miso, int mosi)
{
    if (sck < 0 || miso < 0 || mosi < 0 || sck > 0xFE || miso > 0xFE || mosi > 0xFE) {
        return false;
    }
    const uint8_t want[CH32RV_ROUTE_PINS] = {(uint8_t)CH32RV_PIN_RESOLVE(sck),
                                             (uint8_t)CH32RV_PIN_RESOLVE(miso),
                                             (uint8_t)CH32RV_PIN_RESOLVE(mosi)};
    const Found f = find_route(_number, want, this);
    if (f.index < 0) {
        return false;
    }
    _want = f.row;
    _want_index = (uint8_t)f.index;
    if (_started) {
        const uint8_t old[3] = {_sck_pin, _miso_pin, _mosi_pin};
        end();
        for (int i = 0; i < 3; i++) {
            release_pin(old[i]);
        }
        begin();
    }
    return true;
}

/* ------------------------------------------------- for CH32RVSPISlave */
bool ch32rv_spi_claim(const void *who, int sck, int miso, int mosi, ch32rv_spi_claim_t *out)
{
    if (sck < 0 || miso < 0 || mosi < 0 || sck > 0xFE || miso > 0xFE || mosi > 0xFE) {
        return false;
    }
    const uint8_t want[CH32RV_ROUTE_PINS] = {(uint8_t)CH32RV_PIN_RESOLVE(sck),
                                             (uint8_t)CH32RV_PIN_RESOLVE(miso),
                                             (uint8_t)CH32RV_PIN_RESOLVE(mosi)};
    const Found f = find_route(0, want, who);
    if (f.index < 0) {
        return false;
    }
    const SpiDesc &d = spis[f.index];
#if !CH32RV_SPI_SINGLE
    owner[f.index] = who;
#endif
    ch32rv_clock_enable_at(d.clken_addr, d.clken_mask);
    ch32rv_clock_enable(AFIO);
    if (d.remap_mask) {
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~d.remap_mask) | f.row->value;
    }
    if (d.remap2_mask) {
        CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~d.remap2_mask) | f.row->value2;
    }
    for (int i = 0; i < 3; i++) {
        ch32rv_pin_set_af(want[i], f.row->value);
        ch32rv_gpio_clock_enable((uint8_t)CH32RV_PIN_PORT(want[i]));
    }
    out->base = d.base;
    out->clken_addr = d.clken_addr;
    out->clken_mask = d.clken_mask;
    out->index = (uint8_t)f.index;
    out->number = d.number;
    out->sck = want[0];
    out->miso = want[1];
    out->mosi = want[2];
    return true;
}

void ch32rv_spi_release(const void *who, const ch32rv_spi_claim_t *c)
{
    CH32RV_SPI_CTLR1(c->base) = 0;
    CH32RV_SPI_CTLR2(c->base) = 0;
    ch32rv_clock_disable_at(c->clken_addr, c->clken_mask);
#if !CH32RV_SPI_SINGLE
    if (owner[c->index] == who) {
        owner[c->index] = nullptr;
    }
#else
    (void)who;
#endif
    release_pin(c->sck);
    release_pin(c->miso);
    release_pin(c->mosi);
}
