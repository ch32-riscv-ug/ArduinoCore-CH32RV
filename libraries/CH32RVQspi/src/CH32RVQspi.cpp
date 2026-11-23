/* CH32RVQspi.h. The controller is the STM32 QUADSPI's (CR, DCR, SR, FCR, DLR,
 * CCR, AR, ABR, DR at 0x00 .. 0x20, the same fields), with two CH32 bits in
 * CR: START (bit 5) begins the command CCR describes, and SIOXEN (bit 13)
 * puts IO2 / IO3 to work for the 4-line phases (EVT ch32v205_qspi.c,
 * QSPI_Start / QSPI_EnableQuad). Polled: FTF says the FIFO wants or has a
 * byte, TCF the command is done. */
#include "CH32RVQspi.h"

#if CH32RV_HAS_QSPI

#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#define Q(b, off) CH32RV_REG32((b) + (off))
#define CR(b)   Q(b, 0x00u)
#define DCR(b)  Q(b, 0x04u)
#define SR(b)   Q(b, 0x08u)
#define FCR(b)  Q(b, 0x0Cu)
#define DLR(b)  Q(b, 0x10u)
#define CCR(b)  Q(b, 0x14u)
#define AR(b)   Q(b, 0x18u)
#define ABR(b)  Q(b, 0x1Cu)
#define DR8(b)  CH32RV_REG8((b) + 0x20u)

#define CR_EN       (1u << 0)
#define CR_ABORT    (1u << 1)
#define CR_START    (1u << 5)
#define CR_FSEL     (1u << 7)
#define CR_FTHRES(n) ((uint32_t)(n) << 8)
#define CR_SIOXEN   (1u << 13)
#define CR_PRESC(n) ((uint32_t)(n) << 24)
#define DCR_CKMODE  (1u << 0)
#define DCR_CSHT(n) ((uint32_t)(n) << 8)
#define DCR_FSIZE(n) ((uint32_t)(n) << 16)
#define SR_TEF  (1u << 0)
#define SR_TCF  (1u << 1)
#define SR_FTF  (1u << 2)
#define SR_BUSY (1u << 5)
#define SR_IDLEF (1u << 16)
#define FMODE_WRITE  (0u << 26)
#define FMODE_READ   (1u << 26)
#define FMODE_MAPPED (3u << 26)

namespace {

struct Pad {
    uint8_t role;   /* 0 SCK, 1 SCSN, 2-5 SIO0-3, 6 SCSXN, 7-10 SIOX0-3 */
    uint8_t pad;
    uint32_t value, value2;
};

#ifdef CH32RV_QSPI1_PADS
const Pad q1_pads[] = CH32RV_QSPI1_PADS;
#define Q1_PADS q1_pads, CH32RV_QSPI1_PAD_COUNT
#else
#define Q1_PADS nullptr, 0
#endif
#ifdef CH32RV_QSPI2_PADS
const Pad q2_pads[] = CH32RV_QSPI2_PADS;
#define Q2_PADS q2_pads, CH32RV_QSPI2_PAD_COUNT
#else
#define Q2_PADS nullptr, 0
#endif

struct Instance {
    uint32_t base;
    uint32_t clken_addr, clken_mask;
    const Pad *pads;
    uint8_t pad_count;
    uintptr_t mapped;   /* 0: not documented */
};

const Instance instances[] = {
#if defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
    {0x40024C00u, CH32RV_CLKEN_QSPI1_ADDR, CH32RV_CLKEN_QSPI1_MASK, Q1_PADS, 0x90000000u},
    {0x40025000u, CH32RV_CLKEN_QSPI2_ADDR, CH32RV_CLKEN_QSPI2_MASK, Q2_PADS, 0u},
#else
    {0x40024C00u, CH32RV_CLKEN_QSPI1_ADDR, CH32RV_CLKEN_QSPI1_MASK, Q1_PADS, 0u},
#endif
};
constexpr uint8_t COUNT = sizeof(instances) / sizeof(instances[0]);
CH32RVQspi *owners[COUNT];

uint32_t lines_field(uint8_t lines)
{
    return lines == 0 ? 0u : lines == 1 ? 1u : lines == 2 ? 2u : 3u;
}

}   // namespace

/* `pin` as the controller's pad in `role` (an alternate-function output; the
 * IO pads turn round by themselves). */
bool CH32RVQspi::pad(int pin, uint8_t role)
{
    const Instance &t = instances[_index];
    for (uint8_t i = 0; i < t.pad_count; i++) {
        if (t.pads[i].role == role && t.pads[i].pad == (uint8_t)pin) {
            ch32rv_pin_set_af(t.pads[i].pad, t.pads[i].value);
            const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
            ch32rv_gpio_clock_enable(port);
            ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin), CH32RV_GPIO_CFG_AF_PP_50M);
            return true;
        }
    }
    return false;
}

bool CH32RVQspi::begin(int sck, int cs, int io0, int io1, int io2, int io3)
{
    end();
    if (_number < 1 || _number > COUNT || owners[_number - 1] || sck < 0 || cs < 0 || io0 < 0 || io1 < 0 ||
        (io2 < 0) != (io3 < 0)) {
        return false;
    }
    _index = (int8_t)(_number - 1);
    const Instance &t = instances[_index];
    /* which flash: CS on SCSN (the first) or SCSXN (the second) */
    bool second = false;
    for (uint8_t i = 0; i < t.pad_count; i++) {
        if (t.pads[i].pad == (uint8_t)cs && t.pads[i].role == 6) {
            second = true;
        }
    }
    const uint8_t io_role = second ? 7u : 2u;
    const int io[4] = {io0, io1, io2, io3};
    bool ok = pad(sck, 0) && pad(cs, second ? 6u : 1u);
    for (uint8_t k = 0; k < 4 && ok; k++) {
        if (io[k] >= 0) {
            ok = pad(io[k], (uint8_t)(io_role + k));
        }
    }
    if (!ok) {
        _index = -1;
        return false;
    }
    owners[_index] = this;
    _quad = io2 >= 0;
    const int pins[6] = {sck, cs, io0, io1, io2, io3};
    for (uint8_t k = 0; k < 6; k++) {
        _pins[k] = pins[k];
    }
    ch32rv_clock_enable_at(t.clken_addr, t.clken_mask);
    CR(t.base) = 0;
    DCR(t.base) = DCR_CSHT(1) | DCR_FSIZE(31) | (_mode == 3 ? DCR_CKMODE : 0u);
    CR(t.base) = (second ? CR_FSEL : 0u) | CR_FTHRES(0);
    setClock(_hz ? _hz : 1000000u);
    CR(t.base) |= CR_EN;
    FCR(t.base) = 0x1Fu;
    return true;
}

void CH32RVQspi::end()
{
    if (_index < 0) {
        return;
    }
    const Instance &t = instances[_index];
    CR(t.base) |= CR_ABORT;
    for (uint32_t spin = 0; (CR(t.base) & CR_ABORT) && spin < 100000u; spin++) {
    }
    CR(t.base) = 0;
    ch32rv_clock_disable_at(t.clken_addr, t.clken_mask);
    for (uint8_t k = 0; k < 6; k++) {
        if (_pins[k] >= 0) {
            pinMode(_pins[k], INPUT);
            _pins[k] = -1;
        }
    }
    owners[_index] = nullptr;
    _index = -1;
    _mapped = nullptr;
}

uint32_t CH32RVQspi::setClock(uint32_t hz)
{
    if (hz == 0u) {
        hz = 1u;
    }
    uint32_t div = (CH32RV_HCLK_HZ + hz - 1u) / hz;
    div = div < 1u ? 1u : div > 256u ? 256u : div;
    _hz = CH32RV_HCLK_HZ / div;
    if (_index >= 0) {
        const uint32_t b = instances[_index].base;
        const uint32_t en = CR(b) & CR_EN;
        CR(b) &= ~CR_EN;
        CR(b) = (CR(b) & ~CR_PRESC(0xFFu)) | CR_PRESC(div - 1u);
        CR(b) |= en;
    }
    return _hz;
}

bool CH32RVQspi::setMode(uint8_t mode)
{
    if (mode != 0 && mode != 3) {
        return false;
    }
    _mode = mode;
    if (_index >= 0) {
        const uint32_t b = instances[_index].base;
        const uint32_t en = CR(b) & CR_EN;
        CR(b) &= ~CR_EN;
        DCR(b) = (DCR(b) & ~DCR_CKMODE) | (mode == 3 ? DCR_CKMODE : 0u);
        CR(b) |= en;
    }
    return true;
}

/* No command in progress (a memory mapping ended first). */
bool CH32RVQspi::idle()
{
    const uint32_t b = instances[_index].base;
    if (_mapped || (CCR(b) & FMODE_MAPPED) == FMODE_MAPPED) {
        CR(b) |= CR_ABORT;
        for (uint32_t spin = 0; (CR(b) & CR_ABORT) && spin < 100000u; spin++) {
        }
        _mapped = nullptr;
    }
    const uint32_t t0 = micros();
    while (SR(b) & SR_BUSY) {
        if (micros() - t0 > 100000u) {
            return false;
        }
    }
    return true;
}

bool CH32RVQspi::start(const CH32RVQspiCommand &c, uint32_t fmode, size_t n)
{
    if (_index < 0 || !idle()) {
        return false;
    }
    const uint8_t widest = c.instructionLines > c.addressLines ? c.instructionLines : c.addressLines;
    const uint8_t w = widest > c.alternateLines ? widest : c.alternateLines;
    const uint8_t data = (n || fmode == FMODE_MAPPED) ? c.dataLines : 0u;
    if ((w == 4 || data == 4) && !_quad) {
        return false;   /* no IO2 / IO3 */
    }
    if (c.addressBytes < 1 || c.addressBytes > 4 || c.alternateBytes < 1 || c.alternateBytes > 4 || c.dummyCycles > 31) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    FCR(b) = 0x1Fu;
    if (w == 4 || data == 4) {
        CR(b) |= CR_SIOXEN;
    } else {
        CR(b) &= ~CR_SIOXEN;
    }
    if (n) {
        DLR(b) = (uint32_t)(n - 1u);
    }
    CCR(b) = fmode | (lines_field(data) << 24) | ((uint32_t)c.dummyCycles << 18) |
             ((uint32_t)(c.alternateBytes - 1u) << 16) | (lines_field(c.alternateLines) << 14) |
             ((uint32_t)(c.addressBytes - 1u) << 12) | (lines_field(c.addressLines) << 10) |
             (lines_field(c.instructionLines) << 8) | c.instruction;
    if (c.alternateLines) {
        ABR(b) = c.alternate;
    }
    if (c.addressLines) {
        AR(b) = c.address;
    }
    CR(b) |= CR_START;
    return true;
}

/* The command complete, its flags cleared. */
void CH32RVQspi::finish()
{
    const uint32_t b = instances[_index].base;
    const uint32_t t0 = micros();
    while (!(SR(b) & SR_TCF) && micros() - t0 < 100000u) {
    }
    FCR(b) = 0x1Fu;
}

bool CH32RVQspi::command(const CH32RVQspiCommand &cmd)
{
    if (!start(cmd, FMODE_READ, 0)) {
        return false;
    }
    finish();
    return true;
}

bool CH32RVQspi::write(const CH32RVQspiCommand &cmd, const void *data, size_t n)
{
    if (!data || !n || !start(cmd, FMODE_WRITE, n)) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    const uint8_t *p = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < n; i++) {
        const uint32_t t0 = micros();
        while (!(SR(b) & SR_FTF)) {
            if (micros() - t0 > 100000u) {
                return false;
            }
        }
        DR8(b) = p[i];
    }
    finish();
    return true;
}

bool CH32RVQspi::read(const CH32RVQspiCommand &cmd, void *data, size_t n)
{
    if (!data || !n || !start(cmd, FMODE_READ, n)) {
        return false;
    }
    const uint32_t b = instances[_index].base;
    uint8_t *p = static_cast<uint8_t *>(data);
    for (size_t i = 0; i < n; i++) {
        const uint32_t t0 = micros();
        while (!(SR(b) & (SR_FTF | SR_TCF))) {
            if (micros() - t0 > 100000u) {
                return false;
            }
        }
        p[i] = DR8(b);
    }
    finish();
    return true;
}

bool CH32RVQspi::memoryMap(const CH32RVQspiCommand &readCommand, uint32_t flashBytes)
{
    if (_index < 0 || !instances[_index].mapped || flashBytes < 2u) {
        return false;
    }
    uint32_t fsize = 0;
    while (fsize < 31u && (2u << fsize) < flashBytes) {
        fsize++;
    }
    const uint32_t b = instances[_index].base;
    if (!idle()) {
        return false;
    }
    DCR(b) = (DCR(b) & ~DCR_FSIZE(0x1Fu)) | DCR_FSIZE(fsize);
    if (!start(readCommand, FMODE_MAPPED, 0)) {
        return false;
    }
    _mapped = reinterpret_cast<const volatile uint8_t *>(instances[_index].mapped);
    return true;
}

#endif   // CH32RV_HAS_QSPI
