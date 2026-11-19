#include "CH32RVPioc.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#include <string.h>

namespace {

#if defined(CH32RV_PIOC_BASE) && defined(CH32RV_PIOC_PADS) && defined(CH32RV_CLKEN_PIOC_ADDR) && \
    CH32RV_GPIO_PER_PIN_AF
#define CH32RV_PIOC_READY 1

struct PiocPad {
    uint8_t role, pin, value;      /* value: the pad's AF number */
};
const PiocPad pads[] = CH32RV_PIOC_PADS;

constexpr uint32_t SRAM_BYTES = 4096;
/* SFR_SYS_CFG, the host's bits (WCH's PIOC header) */
constexpr uint8_t SYS_INT_REQ = 0x80, SYS_IO_EN1 = 0x08, SYS_IO_EN0 = 0x04, SYS_RESET = 0x02,
                  SYS_CLK_GATE = 0x01;
constexpr uint8_t REG_SYS_CFG = 0x1C, REG_CTRL_RD = 0x1D, REG_CTRL_WR = 0x1E, REG_DATA0 = 0x20;

/* The host sees the PIOC's SFR n at its base + n. */
volatile uint8_t &sfr(uint8_t n)
{
    return *reinterpret_cast<volatile uint8_t *>(CH32RV_PIOC_BASE + n);
}
#endif

}  // namespace

bool CH32RVPioc::route(int pin, uint8_t role)
{
#if defined(CH32RV_PIOC_READY)
    if (pin < 0) {
        return true;
    }
    const uint8_t want = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    for (const PiocPad &p : pads) {
        if (p.role == role && p.pin == want) {
            const uint8_t port = (uint8_t)CH32RV_PIN_PORT(want), bit = (uint8_t)CH32RV_PIN_BIT(want);
            ch32rv_gpio_clock_enable(port);
            ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);
            ch32rv_pin_set_af(want, p.value);
            _pads[role] = (int8_t)want;
            return true;
        }
    }
#else
    (void)pin;
    (void)role;
#endif
    return false;
}

bool CH32RVPioc::begin(const Image &image, int io0, int io1)
{
#if defined(CH32RV_PIOC_READY)
    end();
    if (!image.words || image.bytes < 4 || image.bytes > SRAM_BYTES) {
        return false;
    }
    /* The pads first: a pad named that the PIOC cannot reach fails before
     * anything changes. */
    const uint8_t want0 = io0 < 0 ? 0xFF : (uint8_t)CH32RV_PIN_RESOLVE(io0);
    const uint8_t want1 = io1 < 0 ? 0xFF : (uint8_t)CH32RV_PIN_RESOLVE(io1);
    bool ok0 = io0 < 0, ok1 = io1 < 0;
    for (const PiocPad &p : pads) {
        ok0 |= p.role == 0 && p.pin == want0;
        ok1 |= p.role == 1 && p.pin == want1;
    }
    if (!ok0 || !ok1) {
        return false;
    }
    CH32RV_REG32(CH32RV_CLKEN_PIOC_ADDR) |= CH32RV_CLKEN_PIOC_MASK;
    const uint8_t io = (uint8_t)((io0 >= 0 ? SYS_IO_EN0 : 0) | (io1 >= 0 ? SYS_IO_EN1 : 0));
    /* Held in reset while its SRAM is written (EVT: RGB1W_Init) */
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_RESET | io);
    memcpy(reinterpret_cast<void *>(CH32RV_PIOC_SRAM_BASE), image.words, image.bytes);
    route(io0, 0);
    route(io1, 1);
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_CLK_GATE | io);
    _begun = true;
    return true;
#else
    (void)image;
    (void)io0;
    (void)io1;
    return false;
#endif
}

void CH32RVPioc::end()
{
#if defined(CH32RV_PIOC_READY)
    if (!_begun) {
        return;
    }
    sfr(REG_SYS_CFG) = SYS_RESET;          /* stopped, the pads the host's again */
    CH32RV_REG32(CH32RV_CLKEN_PIOC_ADDR) &= ~CH32RV_CLKEN_PIOC_MASK;
    for (int8_t &pin : _pads) {
        if (pin >= 0) {
            ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                                   CH32RV_GPIO_CFG_IN_FLOAT);
            ch32rv_pin_set_af((uint8_t)pin, 0);
            pin = -1;
        }
    }
    _begun = false;
#endif
}

void CH32RVPioc::command(uint8_t value)
{
#if defined(CH32RV_PIOC_READY)
    if (_begun) {
        sfr(REG_CTRL_WR) = value;
    }
#else
    (void)value;
#endif
}

bool CH32RVPioc::done() const
{
#if defined(CH32RV_PIOC_READY)
    return _begun && (sfr(REG_SYS_CFG) & SYS_INT_REQ);
#else
    return false;
#endif
}

uint8_t CH32RVPioc::result()
{
#if defined(CH32RV_PIOC_READY)
    return _begun ? sfr(REG_CTRL_RD) : 0;
#else
    return 0;
#endif
}

int CH32RVPioc::waitDone(uint32_t timeoutMs)
{
    const uint32_t start = millis();
    while (!done()) {
        if (!_begun || millis() - start >= timeoutMs) {
            return -1;
        }
    }
    return result();
}

void CH32RVPioc::writeData(uint8_t index, const void *data, size_t n)
{
#if defined(CH32RV_PIOC_READY)
    const uint8_t *in = static_cast<const uint8_t *>(data);
    for (size_t i = 0; _begun && i < n && index + i < 32u; i++) {
        sfr((uint8_t)(REG_DATA0 + index + i)) = in[i];
    }
#else
    (void)index;
    (void)data;
    (void)n;
#endif
}

void CH32RVPioc::readData(uint8_t index, void *data, size_t n) const
{
#if defined(CH32RV_PIOC_READY)
    uint8_t *out = static_cast<uint8_t *>(data);
    for (size_t i = 0; i < n && index + i < 32u; i++) {
        out[i] = _begun ? sfr((uint8_t)(REG_DATA0 + index + i)) : 0;
    }
#else
    (void)index;
    (void)data;
    (void)n;
#endif
}
