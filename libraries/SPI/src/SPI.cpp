#include "SPI.h"

#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

using namespace arduino;

/* Both APB prescalers are /1 in Milestone 1, so either bus runs at F_CPU.
 * When a prescaler becomes configurable this has to follow the instance's own
 * bus (docs/todo.ja.md). */
static const uint32_t CH32_SPI_PCLK = F_CPU;

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
        if (CH32_SPI_PCLK >> (br + 1) <= clock_hz) {
            return br;
        }
    }
    return 7u;
}

void CH32SPIClass::begin()
{
    if (_on_apb1) {
        CH32_RCC_APB1PCENR |= _clock_bit;
    } else {
        CH32_RCC_APB2PCENR |= _clock_bit;
    }
    CH32_RCC_APB2PCENR |= CH32_RCC_APB2_AFIO;
    if (_remap_mask) {
        CH32_AFIO_PCFR1 = (CH32_AFIO_PCFR1 & ~_remap_mask) | _remap_value;
    }
    if (_remap2_mask) {
        CH32_AFIO_PCFR2 = (CH32_AFIO_PCFR2 & ~_remap2_mask) | _remap2_value;
    }

    const uint8_t sck_port = (uint8_t)CH32_PIN_PORT(_sck_pin);
    const uint8_t miso_port = (uint8_t)CH32_PIN_PORT(_miso_pin);
    const uint8_t mosi_port = (uint8_t)CH32_PIN_PORT(_mosi_pin);
    ch32_gpio_clock_enable(sck_port);
    ch32_gpio_clock_enable(miso_port);
    ch32_gpio_clock_enable(mosi_port);
    ch32_gpio_set_config(sck_port, (uint8_t)CH32_PIN_BIT(_sck_pin),
                         CH32_GPIO_CFG_AF_PP_50M);
    ch32_gpio_set_config(mosi_port, (uint8_t)CH32_PIN_BIT(_mosi_pin),
                         CH32_GPIO_CFG_AF_PP_50M);
    /* MISO is driven by the device, so it stays an input. Pulled up rather
     * than floating: with nothing connected a floating input picks up noise
     * and reads back as random bytes instead of a steady 0xFF, which makes
     * "no device" look like "flaky device". */
    ch32_gpio_set_config(miso_port, (uint8_t)CH32_PIN_BIT(_miso_pin),
                         CH32_GPIO_CFG_IN_PULL);
    ch32_gpio_set(miso_port, (uint8_t)CH32_PIN_BIT(_miso_pin));

    _started = true;
    apply(_clock_hz, _order, _mode);
}

void CH32SPIClass::end()
{
    CH32_SPI_CTLR1(_base) = 0;
    if (_on_apb1) {
        CH32_RCC_APB1PCENR &= ~_clock_bit;
    } else {
        CH32_RCC_APB2PCENR &= ~_clock_bit;
    }
    _started = false;
}

void CH32SPIClass::apply(uint32_t clock_hz, BitOrder order, uint8_t mode)
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
    uint16_t ctlr1 = CH32_SPI_CTLR1_MSTR | CH32_SPI_CTLR1_SSM |
                     CH32_SPI_CTLR1_SSI |
                     (uint16_t)(br_for(clock_hz) << CH32_SPI_CTLR1_BR_SHIFT);
    if (mode & 0x1u) {
        ctlr1 |= CH32_SPI_CTLR1_CPHA;
    }
    if (mode & 0x2u) {
        ctlr1 |= CH32_SPI_CTLR1_CPOL;
    }
    if (order == LSBFIRST) {
        ctlr1 |= CH32_SPI_CTLR1_LSBFIRST;
    }

    CH32_SPI_CTLR1(_base) = 0;        /* SPE off while the settings change */
    CH32_SPI_CTLR2(_base) = 0;
    CH32_SPI_CTLR1(_base) = ctlr1 | CH32_SPI_CTLR1_SPE;
}

void CH32SPIClass::beginTransaction(SPISettings settings)
{
    apply(settings.getClockFreq(), settings.getBitOrder(),
          (uint8_t)settings.getDataMode());
}

void CH32SPIClass::endTransaction(void)
{
    /* The bus is left configured and enabled. Arduino's contract is only that
     * another device may now claim it, and re-applying settings is what
     * beginTransaction does. */
}

void CH32SPIClass::setBitOrder(BitOrder order)
{
    apply(_clock_hz, order, _mode);
}

void CH32SPIClass::setDataMode(uint8_t mode)
{
    apply(_clock_hz, _order, (uint8_t)(mode & 0x3u));
}

void CH32SPIClass::setClockDivider(uint32_t divider)
{
    apply(divider ? CH32_SPI_PCLK / divider : CH32_SPI_PCLK / 256u,
          _order, _mode);
}

uint8_t CH32SPIClass::transfer(uint8_t data)
{
    if (!_started) {
        return 0;
    }
    /* Full duplex: one byte out is one byte in, and the wait for RXNE is what
     * makes the call synchronous. No timeout, because unlike I2C there is no
     * other party that can stall the clock - the master generates it. */
    while ((CH32_SPI_STATR(_base) & CH32_SPI_STATR_TXE) == 0u) {
    }
    CH32_SPI_DATAR(_base) = data;
    while ((CH32_SPI_STATR(_base) & CH32_SPI_STATR_RXNE) == 0u) {
    }
    return (uint8_t)CH32_SPI_DATAR(_base);
}

uint16_t CH32SPIClass::transfer16(uint16_t data)
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

void CH32SPIClass::transfer(void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        p[i] = transfer(p[i]);
    }
}

/* --------------------------------------------------------------- routes */
namespace {

struct RouteTable {
    const ch32_route_t *rows;
    uint8_t count;
};

RouteTable routes_for(uint32_t base)
{
#if defined(CH32_SPI1_ROUTES)
    static const ch32_route_t r1[] = CH32_SPI1_ROUTES;
    if (base == CH32_SPI1_BASE) {
        return {r1, CH32_SPI1_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SPI2_ROUTES)
    static const ch32_route_t r2[] = CH32_SPI2_ROUTES;
    if (base == CH32_SPI2_BASE) {
        return {r2, CH32_SPI2_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SPI3_ROUTES)
    static const ch32_route_t r3[] = CH32_SPI3_ROUTES;
    if (base == CH32_SPI3_BASE) {
        return {r3, CH32_SPI3_ROUTE_COUNT};
    }
#endif
    (void)base;
    return {nullptr, 0};
}

void release_pin(uint8_t pin)
{
    ch32_gpio_set_config((uint8_t)CH32_PIN_PORT(pin), (uint8_t)CH32_PIN_BIT(pin),
                         CH32_GPIO_CFG_IN_FLOAT);
}

}  // namespace

bool CH32SPIClass::use_route(const ch32_route_t &route)
{
    const uint8_t old[3] = {_sck_pin, _miso_pin, _mosi_pin};
    const bool was_started = _started;

    if (was_started) {
        end();
        for (int i = 0; i < 3; i++) {
            release_pin(old[i]);
        }
    }
    _sck_pin = route.pins[0];
    _miso_pin = route.pins[1];
    _mosi_pin = route.pins[2];
    _remap_value = route.value;
    _remap2_value = route.value2;
    if (was_started) {
        begin();
    }
    return true;
}

bool CH32SPIClass::setRoute(uint8_t route)
{
    const RouteTable table = routes_for(_base);
    const int i = ch32_route_find(table.rows, table.count, route);
    if (i < 0) {
        return false;
    }
    return use_route(table.rows[i]);
}

bool CH32SPIClass::setPins(uint8_t sck, uint8_t miso, uint8_t mosi)
{
    const RouteTable table = routes_for(_base);
    const uint8_t want[CH32_ROUTE_PINS] = {sck, miso, mosi};
    const int i = ch32_route_match(table.rows, table.count, want);
    if (i < 0) {
        return false;
    }
    return use_route(table.rows[i]);
}

/* ------------------------------------------------------- instances */
#ifndef CH32_SPI1_REMAP_MASK
#define CH32_SPI1_REMAP_MASK 0u
#define CH32_SPI1_REMAP_VAL  0u
#endif
#ifndef CH32_SPI1_REMAP2_MASK
#define CH32_SPI1_REMAP2_MASK 0u
#define CH32_SPI1_REMAP2_VAL  0u
#endif
#ifndef CH32_SPI2_REMAP_MASK
#define CH32_SPI2_REMAP_MASK 0u
#define CH32_SPI2_REMAP_VAL  0u
#endif
#ifndef CH32_SPI2_REMAP2_MASK
#define CH32_SPI2_REMAP2_MASK 0u
#define CH32_SPI2_REMAP2_VAL  0u
#endif
#ifndef CH32_SPI3_REMAP_MASK
#define CH32_SPI3_REMAP_MASK 0u
#define CH32_SPI3_REMAP_VAL  0u
#endif
#ifndef CH32_SPI3_REMAP2_MASK
#define CH32_SPI3_REMAP2_MASK 0u
#define CH32_SPI3_REMAP2_VAL  0u
#endif

#define CH32_SPI_INSTANCE(name, n, base, apb1, clkbit)                        \
    arduino::CH32SPIClass name(base, apb1, clkbit,                            \
                               CH32_SPI##n##_SCK, CH32_SPI##n##_MISO,         \
                               CH32_SPI##n##_MOSI,                            \
                               CH32_SPI##n##_REMAP_MASK,                      \
                               CH32_SPI##n##_REMAP_VAL,                       \
                               CH32_SPI##n##_REMAP2_MASK,                     \
                               CH32_SPI##n##_REMAP2_VAL)

#if defined(CH32_SPI1_SCK)
CH32_SPI_INSTANCE(SPI, 1, CH32_SPI1_BASE, false, CH32_RCC_APB2_SPI1);
#if defined(CH32_SPI2_SCK)
CH32_SPI_INSTANCE(SPI1, 2, CH32_SPI2_BASE, true, CH32_RCC_APB1_SPI2);
#endif
#if defined(CH32_SPI3_SCK)
CH32_SPI_INSTANCE(SPI2, 3, CH32_SPI3_BASE, true, CH32_RCC_APB1_SPI3);
#endif
#elif defined(CH32_SPI2_SCK)
CH32_SPI_INSTANCE(SPI, 2, CH32_SPI2_BASE, true, CH32_RCC_APB1_SPI2);
#if defined(CH32_SPI3_SCK)
CH32_SPI_INSTANCE(SPI1, 3, CH32_SPI3_BASE, true, CH32_RCC_APB1_SPI3);
#endif
#endif
