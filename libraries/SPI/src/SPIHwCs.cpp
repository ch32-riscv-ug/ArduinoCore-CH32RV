/* setHwCs() (arduino-esp32's hardware chip select) linked on demand: the
 * bus calls through ch32rv_spi_hwcs, which only setHwCs() sets, so a sketch
 * that never calls it carries none of this (with LTO the bus's tests of the
 * pointer fold away). */
#include "SPI.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

using namespace arduino;

/* The SPIs in the table, in SPI.cpp's order (ch32rv_instances.h). */
#if CH32RV_SPI_HW_1
#define CH32RV_SPI_HAS_1 1
#endif
#if CH32RV_SPI_HW_2
#define CH32RV_SPI_HAS_2 1
#endif
#if CH32RV_SPI_HW_3
#define CH32RV_SPI_HAS_3 1
#endif

namespace {

/* Each SPI's NSS pad on each route (the variant's CH32RV_SPIn_NSS_PADS:
 * SCK, MISO, MOSI, NSS). Only setHwCs() reads it. */
struct NssRow {
    uint32_t value;            /* the route's value (route_value()) */
    uint8_t pins[4];           /* SCK, MISO, MOSI, NSS */
};
struct SpiNss {
    const NssRow *rows;
    uint8_t count;
};
#define CH32RV_SPI_NSS_SET(n) { spi##n##_nss, (uint8_t)(sizeof spi##n##_nss / sizeof spi##n##_nss[0]) },
#if defined(CH32RV_SPI_HAS_1) && defined(CH32RV_SPI1_NSS_PADS)
const NssRow spi1_nss[] = CH32RV_SPI1_NSS_PADS;
#define CH32RV_SPI_NSS_1 CH32RV_SPI_NSS_SET(1)
#else
#define CH32RV_SPI_NSS_1 { nullptr, 0 },
#endif
#if defined(CH32RV_SPI_HAS_2) && defined(CH32RV_SPI2_NSS_PADS)
const NssRow spi2_nss[] = CH32RV_SPI2_NSS_PADS;
#define CH32RV_SPI_NSS_2 CH32RV_SPI_NSS_SET(2)
#else
#define CH32RV_SPI_NSS_2 { nullptr, 0 },
#endif
#if defined(CH32RV_SPI_HAS_3) && defined(CH32RV_SPI3_NSS_PADS)
const NssRow spi3_nss[] = CH32RV_SPI3_NSS_PADS;
#define CH32RV_SPI_NSS_3 CH32RV_SPI_NSS_SET(3)
#else
#define CH32RV_SPI_NSS_3 { nullptr, 0 },
#endif
#if defined(CH32RV_SPI_HAS_1) || defined(CH32RV_SPI_HAS_2) || defined(CH32RV_SPI_HAS_3)
const SpiNss spi_nss[] = {
#ifdef CH32RV_SPI_HAS_1
    CH32RV_SPI_NSS_1
#endif
#ifdef CH32RV_SPI_HAS_2
    CH32RV_SPI_NSS_2
#endif
#ifdef CH32RV_SPI_HAS_3
    CH32RV_SPI_NSS_3
#endif
};
#else
const SpiNss spi_nss[1] = { { nullptr, 0 } };
#endif


/* endTransaction(): SPE off takes NSS high, once the last byte is out. */
void hwcs_end(CH32RVSPIClass &bus)
{
    bus.hwcs_release();
}

}  // namespace

void CH32RVSPIClass::hwcs_release()
{
    while ((CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_TXE) == 0u) {
    }
    while (CH32RV_SPI_STATR(_base) & CH32RV_SPI_STATR_BSY) {
    }
    CH32RV_SPI_CTLR1(_base) = (uint16_t)(CH32RV_SPI_CTLR1(_base) & ~CH32RV_SPI_CTLR1_SPE);
}

/* The NSS pad of the route the bus is on (begin()'s ss when given, which has
 * to be that pad). */
bool CH32RVSPIClass::nss_pad(uint8_t &pin)
{
    if (!_slot) {
        return false;
    }
    const SpiNss &set = spi_nss[slot_index()];
    for (uint8_t i = 0; i < set.count; i++) {
        const uint8_t *r = set.rows[i].pins;
        if (r[0] == _sck_pin && r[1] == _miso_pin && r[2] == _mosi_pin && set.rows[i].value == route_value() &&
            (!_ss1 || (uint8_t)CH32RV_PIN_RESOLVE(_ss1 - 1) == r[3])) {
            pin = r[3];
            return true;
        }
    }
    return false;
}

void CH32RVSPIClass::nss_connect(bool on)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(_nss_pin), bit = (uint8_t)CH32RV_PIN_BIT(_nss_pin);
    if (on) {
        ch32rv_pin_set_af(_nss_pin, route_value());   /* the route's, as SCK's */
        ch32rv_gpio_clock_enable(port);
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);
    } else {
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_IN_FLOAT);
    }
}

void CH32RVSPIClass::setHwCs(bool use)
{
    if (use == _hwcs) {
        return;
    }
    if (use) {
        uint8_t pin;
        if (!_started || _periph || !nss_pad(pin)) {
            return;
        }
        _nss_pin = pin;
        ch32rv_spi_hwcs = hwcs_end;
        _hwcs = true;
        nss_connect(true);
    } else {
        _hwcs = false;
        nss_connect(false);
    }
    /* the settings again, with or without SSM / SSOE */
    const uint16_t keep = CH32RV_SPI_CTLR1(_base);
    CH32RV_SPI_CTLR1(_base) = (uint16_t)(keep & ~CH32RV_SPI_CTLR1_SPE);
    apply(_clock_hz, _order, _mode);
    if (_hwcs) {
        /* apply() leaves SPE on, which with SSOE holds NSS low: off until the next beginTransaction(), so CS is
         * high outside a transaction from the start (L103 showed it low until the first endTransaction()). */
        hwcs_release();
    }
}

