/* HardwareSerial's synchronous mode: the USART as an SPI controller, its
 * clock on the route's CK pad (CLKEN), TX the data out, RX the data in. The
 * low end of UsartSPI (libraries/SPI, <UsartSPI.h>), which gives it SPIClass's
 * API; a sketch that never calls beginSynchronous() links none of this.
 *
 * The USART sends the least significant bit first and has no other order
 * (CTLR1 / CTLR2 have no MSBFIRST on any family, device-data registers.csv):
 * UsartSPI reverses the bits for MSBFIRST. LBCL is set so the eighth bit is
 * clocked too, CPOL / CPHA are SPI's. The clock is the baud: the USART's
 * clock / BRR, BRR at least 16 (EVT USART/USART_SynchronousMode).
 *
 * CK pads come from the variant's CH32RV_SERIALn_CK_ROUTES
 * (tools/generate/generate.py, device-data's pinout), matched to the port's
 * route as CTS / RTS are (HardwareSerialMode.cpp). */
#include "HardwareSerial.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_serial_irq.h"

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

namespace {

/* One entry per enabled USART, in HardwareSerial.cpp's table order. */
struct CkSet {
    const ch32rv_route_t *rows;
    uint8_t count;
};

#if CH32RV_USART_ON_1
#ifdef CH32RV_SERIAL1_CK_ROUTES
const ch32rv_route_t usart1_ck[] = CH32RV_SERIAL1_CK_ROUTES;
#define CH32RV_CK_SET_1 { usart1_ck, (uint8_t)(sizeof usart1_ck / sizeof usart1_ck[0]) },
#else
#define CH32RV_CK_SET_1 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_1
#endif
#if CH32RV_USART_ON_2
#ifdef CH32RV_SERIAL2_CK_ROUTES
const ch32rv_route_t usart2_ck[] = CH32RV_SERIAL2_CK_ROUTES;
#define CH32RV_CK_SET_2 { usart2_ck, (uint8_t)(sizeof usart2_ck / sizeof usart2_ck[0]) },
#else
#define CH32RV_CK_SET_2 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_2
#endif
#if CH32RV_USART_ON_3
#ifdef CH32RV_SERIAL3_CK_ROUTES
const ch32rv_route_t usart3_ck[] = CH32RV_SERIAL3_CK_ROUTES;
#define CH32RV_CK_SET_3 { usart3_ck, (uint8_t)(sizeof usart3_ck / sizeof usart3_ck[0]) },
#else
#define CH32RV_CK_SET_3 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_3
#endif
#if CH32RV_USART_ON_4
#ifdef CH32RV_SERIAL4_CK_ROUTES
const ch32rv_route_t usart4_ck[] = CH32RV_SERIAL4_CK_ROUTES;
#define CH32RV_CK_SET_4 { usart4_ck, (uint8_t)(sizeof usart4_ck / sizeof usart4_ck[0]) },
#else
#define CH32RV_CK_SET_4 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_4
#endif
#if CH32RV_USART_ON_5
#ifdef CH32RV_SERIAL5_CK_ROUTES
const ch32rv_route_t usart5_ck[] = CH32RV_SERIAL5_CK_ROUTES;
#define CH32RV_CK_SET_5 { usart5_ck, (uint8_t)(sizeof usart5_ck / sizeof usart5_ck[0]) },
#else
#define CH32RV_CK_SET_5 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_5
#endif
#if CH32RV_USART_ON_6
#ifdef CH32RV_SERIAL6_CK_ROUTES
const ch32rv_route_t usart6_ck[] = CH32RV_SERIAL6_CK_ROUTES;
#define CH32RV_CK_SET_6 { usart6_ck, (uint8_t)(sizeof usart6_ck / sizeof usart6_ck[0]) },
#else
#define CH32RV_CK_SET_6 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_6
#endif
#if CH32RV_USART_ON_7
#ifdef CH32RV_SERIAL7_CK_ROUTES
const ch32rv_route_t usart7_ck[] = CH32RV_SERIAL7_CK_ROUTES;
#define CH32RV_CK_SET_7 { usart7_ck, (uint8_t)(sizeof usart7_ck / sizeof usart7_ck[0]) },
#else
#define CH32RV_CK_SET_7 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_7
#endif
#if CH32RV_USART_ON_8
#ifdef CH32RV_SERIAL8_CK_ROUTES
const ch32rv_route_t usart8_ck[] = CH32RV_SERIAL8_CK_ROUTES;
#define CH32RV_CK_SET_8 { usart8_ck, (uint8_t)(sizeof usart8_ck / sizeof usart8_ck[0]) },
#else
#define CH32RV_CK_SET_8 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_8
#endif
#if CH32RV_USART_ON_9
#ifdef CH32RV_SERIAL9_CK_ROUTES
const ch32rv_route_t usart9_ck[] = CH32RV_SERIAL9_CK_ROUTES;
#define CH32RV_CK_SET_9 { usart9_ck, (uint8_t)(sizeof usart9_ck / sizeof usart9_ck[0]) },
#else
#define CH32RV_CK_SET_9 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_9
#endif
#if CH32RV_USART_ON_10
#ifdef CH32RV_SERIAL10_CK_ROUTES
const ch32rv_route_t usart10_ck[] = CH32RV_SERIAL10_CK_ROUTES;
#define CH32RV_CK_SET_10 { usart10_ck, (uint8_t)(sizeof usart10_ck / sizeof usart10_ck[0]) },
#else
#define CH32RV_CK_SET_10 { nullptr, 0 },
#endif
#else
#define CH32RV_CK_SET_10
#endif

const CkSet ck_sets[] = {
    CH32RV_CK_SET_1
    CH32RV_CK_SET_2
    CH32RV_CK_SET_3
    CH32RV_CK_SET_4
    CH32RV_CK_SET_5
    CH32RV_CK_SET_6
    CH32RV_CK_SET_7
    CH32RV_CK_SET_8
    CH32RV_CK_SET_9
    CH32RV_CK_SET_10
    {nullptr, 0},   /* keeps the array non-empty */
};

/* The CK pad of the route the port is on: the row of the route's own AFIO
 * value (remap families), any row on the per-pin families; 0 when none. */
bool ck_pad(const CkSet &set, const ch32rv_route_t &route, uint8_t &pin, uint8_t &af)
{
    for (uint8_t i = 0; i < set.count; i++) {
        const ch32rv_route_t &row = set.rows[i];
#if !CH32RV_GPIO_PER_PIN_AF
        if (row.value != route.value || row.value2 != route.value2) {
            continue;
        }
#endif
        pin = row.pins[0];
        af = (uint8_t)row.value;
        return true;
    }
    return false;
}

/* The CK pad each port drives while synchronous (0: none), for end. */
uint8_t ck_on[CH32RV_SERIAL_COUNT];

}  // namespace

namespace arduino {

bool CH32RVHardwareSerial::beginSynchronous(uint32_t clockHz, uint8_t mode)
{
    if (!clockHz || mode > 3u) {
        return false;
    }
    /* Already synchronous: the clock and mode change in place, CK stays
     * driven (reopening would let it float for a moment). */
    const bool again = _slot && ck_on[_number];
    uint8_t ck = 0, af = 0;
    if (!again) {
        endSynchronous();
        uint8_t index;
        const ch32rv_route_t *route = plannedRoute(index);
        if (!route || !ck_pad(ck_sets[index], *route, ck, af)) {
            return false;              /* no CK on this route */
        }
        begin(clockHz, SERIAL_8N1);
        if (!_slot) {
            return false;
        }
    }
    const uint32_t base = registers();
    /* SPI's clock is a ceiling: rounded down, and the fastest the USART
     * makes (its clock / 16) for anything above. */
    const uint32_t fck = peripheral() == 1 ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
    uint32_t brr = (fck + clockHz - 1u) / clockHz;
    brr = brr < 16u ? 16u : brr > 0xFFFFu ? 0xFFFFu : brr;
    /* Polled: no interrupt takes the received byte from under
     * transferSynchronous(). CK, its idle level and edge, the last bit's
     * clock, with the USART off. */
    /* Already driving CK: it holds the new idle level as a GPIO while the
     * USART is off (off, the USART lets CK fall: with CPOL 1 a mode or clock
     * change made two edges outside the transaction, X035 2026-11-19). */
    if (again) {
        ck = ck_on[_number];
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(ck), bit = (uint8_t)CH32RV_PIN_BIT(ck);
        if (mode & 2u) {
            ch32rv_gpio_set(port, bit);
        } else {
            ch32rv_gpio_clear(port, bit);
        }
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_OUT_PP_10M);
    }
    /* CPOL, CPHA and LBCL take a write only with the transmitter and receiver
     * off as well (UE alone off, they read back 0: V003 and X035, measured
     * 2026-11-19), so the whole CTLR1 is cleared while CTLR2 is written. */
    const uint32_t irq = ch32rv_irq_save();
    const uint16_t ctlr1 = CH32RV_USART_CTLR1(base);
    CH32RV_USART_CTLR1(base) = 0;
    CH32RV_USART_CTLR3(base) = 0;
    CH32RV_USART_BRR(base) = brr;
    const uint16_t ctlr2 = (uint16_t)(CH32RV_USART_CTLR2_CLKEN | CH32RV_USART_CTLR2_LBCL |
                                      ((mode & 2u) ? CH32RV_USART_CTLR2_CPOL : 0u) |
                                      ((mode & 1u) ? CH32RV_USART_CTLR2_CPHA : 0u));
    CH32RV_USART_CTLR2(base) = ctlr2;
    /* Read back: without LBCL the eighth bit is never clocked and nothing is
     * received (RXNE never set) - a USART that did not keep the bits has no
     * synchronous mode to offer. */
    if (CH32RV_USART_CTLR2(base) != ctlr2) {
        CH32RV_USART_CTLR2(base) = 0;
        CH32RV_USART_CTLR1(base) = ctlr1;
        ch32rv_irq_restore(irq);
        close();
        return false;
    }
    CH32RV_USART_CTLR1(base) = (uint16_t)((ctlr1 & ~(CH32RV_USART_CTLR1_RXNEIE | CH32RV_USART_CTLR1_TXEIE |
                                                      CH32RV_USART_CTLR1_TCIE | CH32RV_USART_CTLR1_IDLEIE)) |
                                          CH32RV_USART_CTLR1_UE);
    ch32rv_irq_restore(irq);
    {
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(ck), bit = (uint8_t)CH32RV_PIN_BIT(ck);
        if (!again) {
            ch32rv_gpio_clock_enable(port);
            ch32rv_pin_set_af(ck, af);
            ck_on[_number] = ck;
        }
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);   /* the USART's again */
    }
    /* Whatever came in while the clock was being set up. */
    (void)CH32RV_USART_STATR(base);
    (void)CH32RV_USART_DATAR(base);
    return true;
}

uint8_t CH32RVHardwareSerial::transferSynchronous(uint8_t data)
{
    if (!_slot || !ck_on[_number]) {
        return 0;
    }
    const uint32_t base = registers();
    while (!(CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TXE)) {
    }
    CH32RV_USART_DATAR(base) = data;
    /* RX is sampled on the same eight clocks. */
    while (!(CH32RV_USART_STATR(base) & CH32RV_USART_STATR_RXNE)) {
    }
    const uint8_t got = (uint8_t)CH32RV_USART_DATAR(base);
    /* And the frame's end: with CPHA 0 the byte is in at the eighth leading edge, before its trailing edge - a CS
     * raised on the return cut the last clock off (V205 at 250 kHz, 2026-11-20). TC (cleared by the status read
     * and the data write above) comes after it; the next frame could not start before then anyway. */
    while (!(CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TC)) {
    }
    return got;
}

int CH32RVHardwareSerial::synchronousClockPin(void) const
{
    return _slot && ck_on[_number] ? (int)ck_on[_number] : -1;
}

void CH32RVHardwareSerial::endSynchronous(void)
{
    const uint8_t ck = ck_on[_number];
    if (!ck) {
        return;
    }
    if (_slot) {
        CH32RV_USART_CTLR2(registers()) = 0;
        close();                       /* every transfer waited for its byte */
    }
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(ck), (uint8_t)CH32RV_PIN_BIT(ck), CH32RV_GPIO_CFG_IN_FLOAT);
    ch32rv_pin_set_af(ck, 0);
    ck_on[_number] = 0;
}

}  // namespace arduino

#endif
