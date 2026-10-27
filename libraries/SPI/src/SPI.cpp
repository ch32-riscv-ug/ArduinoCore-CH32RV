#include "SPI.h"
#include "ch32rv_spi_claim.h"

#include "Arduino.h"
#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"

using namespace arduino;

/* Both supported APB prescalers are /1, so either bus runs at F_CPU. A
 * non-unity setting must provide the clock of the instance's own bus here. */
static const uint32_t CH32RV_SPI_PCLK = F_CPU;

/* BR selects a power-of-two divider from 2 to 256. Pick the first one that
 * does not exceed the requested clock, so asking for 1 MHz on a 48 MHz part
 * gives 750 kHz rather than 1.5 MHz - going over is what breaks a device with
 * a maximum rating. */
static uint16_t br_for(uint32_t clock_hz)
{
    if (clock_hz == 0) {
        return 7u;                                  /* slowest */
    }
    for (uint16_t br = 0; br < 7u; br++) {
        if (CH32RV_SPI_PCLK >> (br + 1) <= clock_hz) {
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

#if CH32RV_SPI_ON_1
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
#if CH32RV_SPI_ON_2
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
#if CH32RV_SPI_ON_3
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

/* A board with one enabled SPI has one bus object (SPI) and one instance, so
 * there is nothing to share and nobody else to own it: no owner table, no
 * search. */
#if CH32RV_SPI_COUNT == 1
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

    const uint8_t sck_port = (uint8_t)CH32RV_PIN_PORT(_sck_pin);
    const uint8_t miso_port = (uint8_t)CH32RV_PIN_PORT(_miso_pin);
    const uint8_t mosi_port = (uint8_t)CH32RV_PIN_PORT(_mosi_pin);
    ch32rv_gpio_clock_enable(sck_port);
    ch32rv_gpio_clock_enable(miso_port);
    ch32rv_gpio_clock_enable(mosi_port);
    ch32rv_gpio_set_config(sck_port, (uint8_t)CH32RV_PIN_BIT(_sck_pin),
                         CH32RV_GPIO_CFG_AF_PP_50M);
    ch32rv_gpio_set_config(mosi_port, (uint8_t)CH32RV_PIN_BIT(_mosi_pin),
                         CH32RV_GPIO_CFG_AF_PP_50M);
    /* MISO is driven by the device, so it stays an input. Pulled up rather
     * than floating: with nothing connected a floating input picks up noise
     * and reads back as random bytes instead of a steady 0xFF, which makes
     * "no device" look like "flaky device". */
    ch32rv_gpio_set_config(miso_port, (uint8_t)CH32RV_PIN_BIT(_miso_pin),
                         CH32RV_GPIO_CFG_IN_PULL);
    ch32rv_gpio_set(miso_port, (uint8_t)CH32RV_PIN_BIT(_miso_pin));

    _started = true;
    apply(_clock_hz, _order, _mode);
}

bool CH32RVSPIClass::begin(int sck, int miso, int mosi, int ss)
{
    (void)ss;
    if (_started) {
        end();
    }
    if (!(sck < 0 && miso < 0 && mosi < 0) && !setPins(sck, miso, mosi)) {
        return false;
    }
    begin();
    return true;
}

uint32_t CH32RVSPIClass::transfer32(uint32_t data)
{
    if (_order == LSBFIRST) {
        const uint32_t lo = transfer16((uint16_t)data);
        const uint32_t hi = transfer16((uint16_t)(data >> 16));
        return (hi << 16) | lo;
    }
    const uint32_t hi = transfer16((uint16_t)(data >> 16));
    const uint32_t lo = transfer16((uint16_t)data);
    return (hi << 16) | lo;
}

void CH32RVSPIClass::transferBytes(const uint8_t *data, uint8_t *out, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        const uint8_t in = transfer(data ? data[i] : 0xFFu);
        if (out) {
            out[i] = in;
        }
    }
}

void CH32RVSPIClass::writeBytes(const uint8_t *data, uint32_t size)
{
    transferBytes(data, nullptr, size);
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

    /* SSM/SSI keep the peripheral in master mode without owning an NSS pin:
     * without them a low NSS input drops it out of master mode (MODF) and the
     * transfer silently never happens. */
    uint16_t ctlr1 = CH32RV_SPI_CTLR1_MSTR | CH32RV_SPI_CTLR1_SSM |
                     CH32RV_SPI_CTLR1_SSI |
                     (uint16_t)(br_for(clock_hz) << CH32RV_SPI_CTLR1_BR_SHIFT);
    if (mode & 0x1u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPHA;
    }
    if (mode & 0x2u) {
        ctlr1 |= CH32RV_SPI_CTLR1_CPOL;
    }
    if (order == LSBFIRST) {
        ctlr1 |= CH32RV_SPI_CTLR1_LSBFIRST;
    }

    CH32RV_SPI_CTLR1(_base) = 0;        /* SPE off while the settings change */
    CH32RV_SPI_CTLR2(_base) = 0;
    CH32RV_SPI_CTLR1(_base) = ctlr1 | CH32RV_SPI_CTLR1_SPE;
}

void CH32RVSPIClass::beginTransaction(SPISettings settings)
{
    apply(settings.getClockFreq(), settings.getBitOrder(),
          (uint8_t)settings.getDataMode());
}

void CH32RVSPIClass::endTransaction(void)
{
    /* The bus is left configured and enabled. Arduino's contract is only that
     * another device may now claim it, and re-applying settings is what
     * beginTransaction does. */
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
    apply(divider ? CH32RV_SPI_PCLK / divider : CH32RV_SPI_PCLK / 256u,
          _order, _mode);
}

uint8_t CH32RVSPIClass::transfer(uint8_t data)
{
    if (!_started) {
        return 0;
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

uint16_t CH32RVSPIClass::transfer16(uint16_t data)
{
    /* Two 8-bit frames rather than switching DFF: the wire result is the same
     * and it keeps one frame size in play, so a driver that mixes transfer()
     * and transfer16() cannot end up half-configured. */
    if (_order == LSBFIRST) {
        const uint16_t low = transfer((uint8_t)(data & 0xFFu));
        const uint16_t high = transfer((uint8_t)(data >> 8));
        return (uint16_t)(low | (high << 8));
    }
    const uint16_t high = transfer((uint8_t)(data >> 8));
    const uint16_t low = transfer((uint8_t)(data & 0xFFu));
    return (uint16_t)((high << 8) | low);
}

void CH32RVSPIClass::transfer(void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        p[i] = transfer(p[i]);
    }
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
