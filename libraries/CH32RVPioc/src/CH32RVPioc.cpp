#include "CH32RVPioc.h"

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_registers.h"

#include <string.h>

namespace {

/* The pads are routed by an AF number per pad (H41x, V205) or by one remap
 * field for both (X035 / X033: AFIO PCFR1). */
#if defined(CH32RV_PIOC_BASE) && defined(CH32RV_PIOC_PADS) && \
    (CH32RV_GPIO_PER_PIN_AF || defined(CH32RV_PIOC_REMAP_MASK))
#define CH32RV_PIOC_READY 1

struct PiocPad {
    uint8_t role, pin, value;      /* value: the pad's AF number, or the remap field's value */
};
const PiocPad pads[] = CH32RV_PIOC_PADS;

/* Where the program SRAM is part of the sketch's RAM (X035's top 4 KiB,
 * V205's bottom), this reservation keeps the linker's other sections out of
 * it (sections.ld); referenced only from begin(), so a sketch without the
 * PIOC keeps its whole RAM. */
#if defined(CH32RV_PIOC_SRAM_SECTION)
__attribute__((section(CH32RV_PIOC_SRAM_SECTION), aligned(4))) uint8_t reserved[4096];
inline uint8_t *program_sram() { return reserved; }
#else
inline uint8_t *program_sram() { return reinterpret_cast<uint8_t *>(CH32RV_PIOC_SRAM_BASE); }
#endif

constexpr uint32_t SRAM_BYTES = 4096;
/* SFR_SYS_CFG, the host's bits (WCH's PIOC header) */
constexpr uint8_t SYS_INT_REQ = 0x80, SYS_IO_EN1 = 0x08, SYS_IO_EN0 = 0x04, SYS_RESET = 0x02,
                  SYS_CLK_GATE = 0x01;
constexpr uint8_t REG_SYS_CFG = 0x1C, REG_CTRL_RD = 0x1D, REG_CTRL_WR = 0x1E, REG_DATA0 = 0x20;

/* cmd.out, and the request taken down: reading cmd.out clears
 * SB_DATA_SW_MR, but SB_INT_REQ only falls when the host writes cmd.out (the
 * data is ignored; WCH's ch32h417_pioc.h, and measured on X035 2026-11-19:
 * after the read SYS_CFG still read 0x85 and the interrupt came back at
 * once). */
uint8_t take_result();

/* The host sees the PIOC's SFR n at its base + n. */
volatile uint8_t &sfr(uint8_t n)
{
    return *reinterpret_cast<volatile uint8_t *>(CH32RV_PIOC_BASE + n);
}

uint8_t take_result()
{
    const uint8_t status = sfr(REG_CTRL_RD);
    sfr(REG_CTRL_RD) = 0;
    /* Read back so the write has landed before an interrupt returns: else
     * the request still stood and the interrupt came once more (all three
     * families, 2026-11-19). */
    (void)sfr(REG_SYS_CFG);
    return status;
}
#endif

/* The object the interrupt serves (onDone()). */
CH32RVPioc *serving;

}  // namespace

#if defined(CH32RV_PIOC_READY) && defined(CH32RV_PIOC_IRQN)
/* The vector table's PIOC entry (weak there): linked with this file. */
extern "C" CH32RV_INTERRUPT void PIOC_IRQHandler(void)
{
    if (serving) {
        serving->service();
    } else {
        (void)take_result();           /* nobody to tell: the request taken down */
    }
}
#endif

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
#if CH32RV_GPIO_PER_PIN_AF
            ch32rv_pin_set_af(want, p.value);
#endif
            _pads[role] = want;
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
    int v0 = io0 < 0 ? -1 : -2, v1 = io1 < 0 ? -1 : -2;      /* -2: not found */
    for (const PiocPad &p : pads) {
        if (p.role == 0 && p.pin == want0) {
            v0 = p.value;
        }
        if (p.role == 1 && p.pin == want1) {
            v1 = p.value;
        }
    }
    if (v0 == -2 || v1 == -2) {
        return false;
    }
#if defined(CH32RV_PIOC_REMAP_MASK)
    /* One field routes both: the two pads must be of one remap value. */
    if (v0 >= 0 && v1 >= 0 && v0 != v1) {
        return false;
    }
    const int remap = v0 >= 0 ? v0 : v1;
#endif
    uint8_t *sram = program_sram();
    if (reinterpret_cast<uintptr_t>(sram) != CH32RV_PIOC_SRAM_BASE) {
        return false;                  /* the linker put the reservation elsewhere */
    }
#if defined(CH32RV_CLKEN_PIOC_ADDR)
    CH32RV_REG32(CH32RV_CLKEN_PIOC_ADDR) |= CH32RV_CLKEN_PIOC_MASK;
#endif
#if defined(CH32RV_PIOC_REMAP_MASK)
    if (remap >= 0) {
        ch32rv_clock_enable(AFIO);
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~CH32RV_PIOC_REMAP_MASK) |
                            ((uint32_t)remap << CH32RV_PIOC_REMAP_SHIFT);
    }
#endif
    const uint8_t io = (uint8_t)((io0 >= 0 ? SYS_IO_EN0 : 0) | (io1 >= 0 ? SYS_IO_EN1 : 0));
    /* Held in reset while its SRAM is written (EVT: RGB1W_Init) */
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_RESET | io);
    memcpy(sram, image.words, image.bytes);
    route(io0, 0);
    route(io1, 1);
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_CLK_GATE | io);
    _imageBytes = (uint16_t)image.bytes;
    _io = io;
    _begun = true;
#if defined(CH32RV_PIOC_IRQN)
    if (_done) {
        serving = this;
        ch32rv_irq_clear_pending(CH32RV_PIOC_IRQN);
        ch32rv_irq_enable(CH32RV_PIOC_IRQN);
    }
#endif
    return true;
#else
    (void)image;
    (void)io0;
    (void)io1;
    return false;
#endif
}

bool CH32RVPioc::loadSram(uint16_t offset, const void *data, size_t n)
{
#if defined(CH32RV_PIOC_READY)
    if (!_begun || (!data && n) || offset < _imageBytes || (size_t)offset + n > SRAM_BYTES) {
        return false;
    }
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_RESET | _io);
    memcpy(program_sram() + offset, data, n);
    sfr(REG_SYS_CFG) = (uint8_t)(SYS_CLK_GATE | _io);
    return true;
#else
    (void)offset;
    (void)data;
    (void)n;
    return false;
#endif
}

void CH32RVPioc::end()
{
#if defined(CH32RV_PIOC_READY)
    if (!_begun) {
        return;
    }
#if defined(CH32RV_PIOC_IRQN)
    if (serving == this) {
        ch32rv_irq_disable(CH32RV_PIOC_IRQN);
        serving = nullptr;
    }
#endif
    sfr(REG_SYS_CFG) = SYS_RESET;          /* stopped, the pads the host's again */
#if defined(CH32RV_CLKEN_PIOC_ADDR)
    CH32RV_REG32(CH32RV_CLKEN_PIOC_ADDR) &= ~CH32RV_CLKEN_PIOC_MASK;
#endif
    for (uint8_t &pin : _pads) {
        if (pin != NO_PAD) {
            ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                                   CH32RV_GPIO_CFG_IN_FLOAT);
            ch32rv_pin_set_af(pin, 0);
            pin = NO_PAD;
        }
    }
    _begun = false;
#endif
}

bool CH32RVPioc::setOpenDrain(uint8_t io, bool on)
{
#if defined(CH32RV_PIOC_READY)
    if (!_begun || io > 1 || _pads[io] == NO_PAD) {
        return false;
    }
    const uint8_t pin = _pads[io];
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                           on ? CH32RV_GPIO_CFG_AF_OD_50M : CH32RV_GPIO_CFG_AF_PP_50M);
    return true;
#else
    (void)io;
    (void)on;
    return false;
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
    return _begun ? take_result() : 0;
#else
    return 0;
#endif
}

void CH32RVPioc::onDone(DoneCallback callback)
{
#if defined(CH32RV_PIOC_READY) && defined(CH32RV_PIOC_IRQN)
    ch32rv_irq_disable(CH32RV_PIOC_IRQN);
    _done = callback;
    if (_done) {
        serving = this;
        if (_begun) {
            /* A request waitDone() took was latched with the interrupt off
             * (the PFIC keeps it): the first call came with an old cmd.out
             * (all three families, 2026-11-19). */
            ch32rv_irq_clear_pending(CH32RV_PIOC_IRQN);
            ch32rv_irq_enable(CH32RV_PIOC_IRQN);
        }
    } else if (serving == this) {
        serving = nullptr;
    }
#else
    (void)callback;
#endif
}

void CH32RVPioc::service()
{
#if defined(CH32RV_PIOC_READY)
    const uint8_t status = take_result();
    if (_done) {
        _done(status);
    }
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
