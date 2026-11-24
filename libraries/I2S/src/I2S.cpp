/* I2S.h. SPIn's I2S mode (the STM32F1-shaped I2SCFGR / I2SPR at 0x1C /
 * 0x20 of the SPI, as EVT ch32v30x_spi.c / ch32h417_spi.c drive them), its
 * data register fed or emptied by the SPI's own DMA lines (the variant's
 * CH32RV_SPIn_DMA_TX / _RX) over a two-half buffer whose half and full
 * interrupts this file handles. */
#include "I2S.h"

#if CH32RV_HAS_I2S

#include <stdlib.h>
#include <string.h>

#include "ch32rv_priority.h"
#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_registers.h"

#define I2S_CAT2(a, b) a##b
#define I2S_CAT(a, b) I2S_CAT2(a, b)

/* I2SCFGR / I2SPR (RM: SPI_I2S_CFGR, SPI_I2SPR) */
#define I2S_CFGR(b) CH32RV_REG16((b) + 0x1Cu)
#define I2S_PR(b)   CH32RV_REG16((b) + 0x20u)
#define CFGR_CHLEN32   (1u << 0)
#define CFGR_DATLEN(n) ((uint32_t)(n) << 1)   /* 0 16-bit, 1 24-bit, 2 32-bit */
#define CFGR_CKPOL     (1u << 3)
#define CFGR_STD(n)    ((uint32_t)(n) << 4)   /* 0 Philips, 1 MSB, 2 LSB, 3 PCM */
#define CFGR_PCMSYNC   (1u << 7)              /* PCM's long frame sync */
#define CFGR_MASTER_TX (2u << 8)
#define CFGR_MASTER_RX (3u << 8)
#define CFGR_I2SE      (1u << 10)
#define CFGR_I2SMOD    (1u << 11)
#define PR_ODD         (1u << 8)
#define PR_MCKOE       (1u << 9)
#define CTLR2_RXDMAEN  (1u << 0)
#define CTLR2_TXDMAEN  (1u << 1)
#define STATR_TXE      (1u << 1)
#define STATR_BSY      (1u << 7)
#define CFGR_16BIT     ((1u << 8) | (1u << 10))   /* DMA PSIZE, MSIZE a halfword */

#ifndef CH32RV_CLOCK_SYSCLK_HZ
#error "CH32RV_CLOCK_SYSCLK_HZ is required: the I2S clock is SYSCLK"
#endif

namespace {

/* The variant's CH32RV_I2Sn_PADS rows. */
struct Pad {
    uint8_t role;     /* 0 CK, 1 WS, 2 SD, 3 MCK */
    uint8_t pad;
    uint32_t value;   /* per-pin: the pad's function number; AFIO: its route's PCFR1 bits */
    uint32_t value2;  /* AFIO: its route's PCFR2 bits */
};

struct Port {
    uint8_t number;
    uint32_t base;
    uint32_t clken_addr;
    uint32_t clken_mask;
    const Pad *pads;
    uint8_t pad_count;
    uint32_t remap_mask;
    uint32_t remap2_mask;
    ch32rv_dma_line_t tx;
    ch32rv_dma_line_t rx;
    uint32_t tx_irqn;
    uint32_t rx_irqn;
};

#ifdef CH32RV_I2S2_PADS
const Pad i2s2_pads[] = CH32RV_I2S2_PADS;
#endif
#ifdef CH32RV_I2S3_PADS
const Pad i2s3_pads[] = CH32RV_I2S3_PADS;
#endif

#ifndef CH32RV_SPI2_REMAP_MASK
#define CH32RV_SPI2_REMAP_MASK 0u
#endif
#ifndef CH32RV_SPI2_REMAP2_MASK
#define CH32RV_SPI2_REMAP2_MASK 0u
#endif
#ifndef CH32RV_SPI3_REMAP_MASK
#define CH32RV_SPI3_REMAP_MASK 0u
#endif
#ifndef CH32RV_SPI3_REMAP2_MASK
#define CH32RV_SPI3_REMAP2_MASK 0u
#endif

const Port ports[] = {
#ifdef CH32RV_I2S2_PADS
    {2, CH32RV_SPI2_BASE, CH32RV_SPI2_CLKEN_ADDR, CH32RV_SPI2_CLKEN_MASK, i2s2_pads, CH32RV_I2S2_PAD_COUNT,
     CH32RV_SPI2_REMAP_MASK,
     CH32RV_SPI2_REMAP2_MASK, CH32RV_SPI2_DMA_TX, CH32RV_SPI2_DMA_RX,
     I2S_CAT(CH32RV_IRQN_, CH32RV_SPI2_DMA_TX_IRQ), I2S_CAT(CH32RV_IRQN_, CH32RV_SPI2_DMA_RX_IRQ)},
#endif
#ifdef CH32RV_I2S3_PADS
    {3, CH32RV_SPI3_BASE, CH32RV_SPI3_CLKEN_ADDR, CH32RV_SPI3_CLKEN_MASK, i2s3_pads, CH32RV_I2S3_PAD_COUNT,
     CH32RV_SPI3_REMAP_MASK,
     CH32RV_SPI3_REMAP2_MASK, CH32RV_SPI3_DMA_TX, CH32RV_SPI3_DMA_RX,
     I2S_CAT(CH32RV_IRQN_, CH32RV_SPI3_DMA_TX_IRQ), I2S_CAT(CH32RV_IRQN_, CH32RV_SPI3_DMA_RX_IRQ)},
#endif
};
constexpr uint8_t PORT_COUNT = sizeof(ports) / sizeof(ports[0]);

I2SClass *owners[PORT_COUNT];

void pad_mode(int pin, uint32_t cfg)
{
    if (pin < 0) {
        return;
    }
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin), cfg);
}

/* The rows of these pins on this port, -1 for an unused role: false when
 * they are not all its pads (and, on the AFIO families, not on one route). */
bool find_pads(const Port &p, const int pins[4], int rows[4])
{
    for (uint8_t i = 0; i < p.pad_count; i++) {
        if (p.pads[i].role != 0 || p.pads[i].pad != (uint8_t)pins[0]) {
            continue;
        }
        bool ok = true;
        rows[0] = i;
        for (uint8_t r = 1; r < 4 && ok; r++) {
            rows[r] = -1;
            if (pins[r] < 0) {
                continue;
            }
            for (uint8_t k = 0; k < p.pad_count; k++) {
                const Pad &c = p.pads[k];
                if (c.role == r && c.pad == (uint8_t)pins[r]
#if !CH32RV_GPIO_PER_PIN_AF
                    && c.value == p.pads[i].value && c.value2 == p.pads[i].value2
#endif
                ) {
                    rows[r] = k;
                    break;
                }
            }
            ok = rows[r] >= 0;
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

/* What the DMA just wrote is not what the compiler last saw there. */
inline void dma_fence()
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

}   // namespace

I2SClass::I2SClass() {}

I2SClass::~I2SClass()
{
    end();
}

void I2SClass::setPins(int bclk, int ws, int dout, int din, int mclk)
{
    _bclk = bclk;
    _ws = ws;
    _dout = dout;
    _din = din;
    _mclk = mclk;
}

void I2SClass::setInverted(bool bclk, bool ws, bool mclk)
{
    _inv_bclk = bclk;
    _inv_ws = ws;
    (void)mclk;
}

void I2SClass::setFormat(CH32RVI2SFormat format)
{
    _format = format;
}

void I2SClass::setBufferSize(size_t bytes)
{
    _half_bytes = bytes < 64u ? 64u : bytes;
}

size_t I2SClass::hwPerFrame() const
{
    if (_sai >= 0) {
        return (size_t)_hw_slots * (_bits == 16 ? 1u : 2u);
    }
    return _bits == 16 ? 2u : 4u;
}

size_t I2SClass::frameBytes() const
{
    return (size_t)(_bits / 8u) * _slots;
}

bool I2SClass::begin(i2s_mode_t mode, uint32_t rate, i2s_data_bit_width_t bits, i2s_slot_mode_t ch,
                     int8_t slot_mask)
{
    end();
    _error = I2S_ERROR_ARG;
    if (mode == I2S_MODE_PDM_RX && _pdm_ops && rate &&
        (bits == I2S_DATA_BIT_WIDTH_16BIT || bits == I2S_DATA_BIT_WIDTH_24BIT ||
         bits == I2S_DATA_BIT_WIDTH_32BIT) && (ch == I2S_SLOT_MODE_MONO || ch == I2S_SLOT_MODE_STEREO)) {
        _bits = (uint8_t)bits;
        _slots = (uint8_t)ch;
        _mask = (uint8_t)(slot_mask < 0 ? I2S_STD_SLOT_LEFT : slot_mask);
        _tx = false;
        _peeked = _peek_pos = 0;
        _overruns = 0;
        if (!_pdm_ops->begin(*this, rate)) {
            return false;
        }
        _pdm_on = true;
        _error = I2S_ERROR_NONE;
        return true;
    }
#ifdef CH32RV_SAI_PADS
    if (mode == I2S_MODE_SPDIF_TX) {
        if (rate == 0 || (bits != I2S_DATA_BIT_WIDTH_16BIT && bits != I2S_DATA_BIT_WIDTH_24BIT &&
                          bits != I2S_DATA_BIT_WIDTH_32BIT) || (ch != I2S_SLOT_MODE_MONO && ch != I2S_SLOT_MODE_STEREO)) {
            return false;
        }
        _error = I2S_ERROR_STATE;
        if (_dout < 0 || _din >= 0) {
            return false;   /* out only */
        }
        _bits = (uint8_t)bits;
        _slots = (uint8_t)ch;
        _mask = (uint8_t)(slot_mask < 0 ? I2S_STD_SLOT_BOTH : (slot_mask & 3));
        _tdm_mask = 0;
        return beginSai(true, rate, false, true);
    }
#endif
    if ((mode != I2S_MODE_STD && mode != I2S_MODE_TDM) || rate == 0 ||
        (bits != I2S_DATA_BIT_WIDTH_16BIT && bits != I2S_DATA_BIT_WIDTH_24BIT &&
         bits != I2S_DATA_BIT_WIDTH_32BIT) ||
        (ch != I2S_SLOT_MODE_MONO && ch != I2S_SLOT_MODE_STEREO) || _format > CH32RVI2SFormat::PcmLong ||
        (mode == I2S_MODE_TDM && slot_mask == 0)) {
        return false;
    }
    _error = I2S_ERROR_STATE;
    if ((_dout >= 0) == (_din >= 0) || _bclk < 0) {
        return false;   /* one direction, and the bit clock */
    }
    const bool tx = _dout >= 0;
    const int pins[4] = {_bclk, _ws, tx ? _dout : _din, _mclk};
    int pi = -1;
    int rows[4];
    for (uint8_t i = 0; i < PORT_COUNT && pi < 0 && mode == I2S_MODE_STD && _ws >= 0; i++) {
        if (!owners[i] && find_pads(ports[i], pins, rows)) {
            pi = i;
        }
    }
    if (pi < 0) {
        /* not an SPI's: an SAI block's (CH32H417), or nothing */
        _bits = (uint8_t)bits;
        _slots = (uint8_t)ch;
        _mask = (uint8_t)(slot_mask < 0 ? (tx ? I2S_STD_SLOT_BOTH : I2S_STD_SLOT_LEFT) : (slot_mask & 3));
        /* TDM: the slots used (ESP_I2S's int8_t: slots 0 .. 6; -1 slot 0, or 0 and 1 in stereo) */
        _tdm_mask = (uint16_t)(mode != I2S_MODE_TDM ? 0 : slot_mask > 0 ? slot_mask
                               : ch == I2S_SLOT_MODE_STEREO ? 0x3 : 0x1);
        return beginSai(tx, rate, mode == I2S_MODE_TDM);
    }
    const Port &p = ports[pi];

    /* The divider: SYSCLK / (32 x slots of 16 / 32 bits x d), or with the
     * master clock SYSCLK / (256 x d); d = 2 x I2SDIV + ODD, I2SDIV 2..255. */
    const uint32_t per = _mclk >= 0 ? 256u : (bits == I2S_DATA_BIT_WIDTH_16BIT ? 32u : 64u);
    const uint64_t den = (uint64_t)per * rate;
    const uint32_t d = (uint32_t)(((uint64_t)CH32RV_CLOCK_SYSCLK_HZ + den / 2u) / den);
    _error = I2S_ERROR_ARG;
    if (d < 4u || d > 511u) {
        return false;
    }

    _error = I2S_ERROR_DMA;
    const ch32rv_dma_line_t &line = tx ? p.tx : p.rx;
    if (!ch32rv_dma_claim(line.dma, line.channel)) {
        return false;
    }
    _bits = (uint8_t)bits;
    _slots = (uint8_t)ch;
    _mask = (uint8_t)(slot_mask < 0 ? (tx ? I2S_STD_SLOT_BOTH : I2S_STD_SLOT_LEFT) : (slot_mask & 3));
    const uint32_t hpf = hwPerFrame();
    _half = (uint32_t)(_half_bytes / 2u / hpf * hpf);
    if (_half < hpf) {
        _half = hpf;
    }
    _ring = (uint16_t *)calloc(2u * _half, sizeof(uint16_t));
    _error = I2S_ERROR_MEMORY;
    if (!_ring) {
        ch32rv_dma_release(line.dma, line.channel);
        return false;
    }
    _tx = tx;
    _port = (int8_t)pi;
    owners[pi] = this;
    _sai = -1;
    _tdm_mask = 0;
    _dma = &line;
    _irqn = tx ? p.tx_irqn : p.rx_irqn;
    _rate = (uint32_t)(CH32RV_CLOCK_SYSCLK_HZ / ((uint64_t)per * d));
    _playing = 0;
    _w_half = 1;
    _w_ofs = 0;
    _fill[0] = _fill[1] = 0;
    _underruns = _overruns = 0;
    _staged = 0;
    _rx_halves = 0;
    _rx_read = 0;
    _rx_off = 0;
    _peeked = _peek_pos = 0;

    /* The SPI clocked and reset, the route selected, the pads given it. */
    ch32rv_clock_enable_at(p.clken_addr, p.clken_mask);
    CH32RV_REG32(p.clken_addr - 0x0Cu) |= p.clken_mask;
    CH32RV_REG32(p.clken_addr - 0x0Cu) &= ~p.clken_mask;
    const Pad &first = p.pads[rows[0]];
    ch32rv_clock_enable(AFIO);
    if (p.remap_mask) {
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~p.remap_mask) | first.value;
    }
#ifdef CH32RV_AFIO_PCFR2
    if (p.remap2_mask) {
        CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~p.remap2_mask) | first.value2;
    }
#endif
    for (uint8_t r = 0; r < 4; r++) {
        if (rows[r] >= 0) {
            ch32rv_pin_set_af(p.pads[rows[r]].pad, p.pads[rows[r]].value);
        }
    }
    pad_mode(_bclk, CH32RV_GPIO_CFG_AF_PP_50M);
    pad_mode(_ws, CH32RV_GPIO_CFG_AF_PP_50M);
    pad_mode(tx ? _dout : _din, tx ? CH32RV_GPIO_CFG_AF_PP_50M : CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_mclk, CH32RV_GPIO_CFG_AF_PP_50M);

    static const uint8_t std_field[] = {0, 1, 2, 3, 3};
    uint32_t cfgr = CFGR_I2SMOD | (tx ? CFGR_MASTER_TX : CFGR_MASTER_RX) | CFGR_STD(std_field[(uint8_t)_format]) |
                    (_format == CH32RVI2SFormat::PcmLong ? CFGR_PCMSYNC : 0u) | (_inv_bclk ? CFGR_CKPOL : 0u);
    if (bits == I2S_DATA_BIT_WIDTH_24BIT) {
        cfgr |= CFGR_DATLEN(1) | CFGR_CHLEN32;
    } else if (bits == I2S_DATA_BIT_WIDTH_32BIT) {
        cfgr |= CFGR_DATLEN(2) | CFGR_CHLEN32;
    }
    I2S_CFGR(p.base) = (uint16_t)cfgr;
    I2S_PR(p.base) = (uint16_t)((d >> 1) | ((d & 1u) ? PR_ODD : 0u) | (_mclk >= 0 ? PR_MCKOE : 0u));

    ch32rv_dma_open(&line);
    CH32RV_DMA_PADDR(&line) = p.base + 0x0Cu;
    CH32RV_DMA_MADDR(&line) = (uint32_t)(uintptr_t)_ring;
    CH32RV_DMA_CNTR(&line) = 2u * _half;
    CH32RV_DMA_CFGR(&line) = CH32RV_DMA_CFGR_PL(3) | CFGR_16BIT | CH32RV_DMA_CFGR_MINC | CH32RV_DMA_CFGR_CIRC |
                             CH32RV_DMA_CFGR_HTIE | CH32RV_DMA_CFGR_TCIE | (tx ? CH32RV_DMA_CFGR_DIR : 0u);
    CH32RV_DMA_CFGR(&line) |= CH32RV_DMA_CFGR_EN;
    const uint32_t irqn = tx ? p.tx_irqn : p.rx_irqn;
    ch32rv_irq_clear_pending(irqn);
    ch32rv_irq_enable(irqn);
    CH32RV_SPI_CTLR2(p.base) = (uint16_t)(tx ? CTLR2_TXDMAEN : CTLR2_RXDMAEN);
    I2S_CFGR(p.base) = (uint16_t)(cfgr | CFGR_I2SE);
    _error = I2S_ERROR_NONE;
    return true;
}

bool I2SClass::end()
{
    if (_pdm_on) {
        _pdm_ops->end(*this);
        _pdm_on = false;
        _rate = 0;
        return true;
    }
    if (_port < 0) {
        return false;
    }
    if (_sai >= 0) {
        endSai();
        return true;
    }
    const Port &p = ports[_port];
    const ch32rv_dma_line_t &line = _tx ? p.tx : p.rx;
    ch32rv_irq_disable(_tx ? p.tx_irqn : p.rx_irqn);
    CH32RV_DMA_CFGR(&line) = 0;
    if (_tx) {
        /* the halfword in the shift register out, as RM's I2S disable asks */
        for (uint32_t t = 0; t < 100000u && !(CH32RV_SPI_STATR(p.base) & STATR_TXE); t++) {
        }
        for (uint32_t t = 0; t < 100000u && (CH32RV_SPI_STATR(p.base) & STATR_BSY); t++) {
        }
    }
    I2S_CFGR(p.base) = 0;
    CH32RV_SPI_CTLR2(p.base) = 0;
    ch32rv_dma_close(&line);
    ch32rv_dma_release(line.dma, line.channel);
    CH32RV_REG32(p.clken_addr - 0x0Cu) |= p.clken_mask;
    CH32RV_REG32(p.clken_addr - 0x0Cu) &= ~p.clken_mask;
    ch32rv_clock_disable_at(p.clken_addr, p.clken_mask);
    pad_mode(_bclk, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_ws, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_tx ? _dout : _din, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_mclk, CH32RV_GPIO_CFG_IN_FLOAT);
    owners[_port] = nullptr;
    _port = -1;
    free(_ring);
    _ring = nullptr;
    _rate = 0;
    return true;
}

/* ------------------------------------------------------------ the samples */

/* One frame of the sketch's into the halfwords the data register takes:
 * each slot left-aligned in 32 bits first, then its high halfword (and for
 * 32-bit slots its low one; 24-bit right-justified wants the top 8 bits in
 * the first halfword's low byte instead). */
/* The SAI's: a halfword a 16-bit slot, else a word (low halfword first: the
 * DMA moves words), right-aligned as DATAR takes them; mono STD on the
 * slots of the mask, TDM one sample a slot used. */
static int32_t sample_in(const uint8_t *p, uint8_t bits)
{
    if (bits == 16) {
        return (int16_t)(p[0] | p[1] << 8);
    }
    if (bits == 24) {
        return (int32_t)((uint32_t)(p[0] | p[1] << 8 | (uint32_t)p[2] << 16) << 8) >> 8;
    }
    return (int32_t)(p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

void I2SClass::packFrame(const uint8_t *in, uint16_t *out) const
{
    if (_sai >= 0) {
        const uint8_t bytes = (uint8_t)(_bits / 8u);
        for (uint8_t k = 0; k < _hw_slots; k++) {
            int32_t v = 0;
            if (_tdm_mask || _slots == 2) {
                v = sample_in(in + k * bytes, _bits);
            } else if (_mask & (1u << k)) {
                v = sample_in(in, _bits);
            }
            if (_bits == 16) {
                *out++ = (uint16_t)v;
            } else {
                *out++ = (uint16_t)v;
                *out++ = (uint16_t)((uint32_t)v >> 16);
            }
        }
        return;
    }
    uint32_t v[2];
    for (uint8_t s = 0; s < _slots; s++) {
        const uint8_t *b = in + s * (_bits / 8u);
        if (_bits == 16) {
            v[s] = (uint32_t)(b[0] | (b[1] << 8)) << 16;
        } else if (_bits == 24) {
            v[s] = ((uint32_t)b[0] << 8) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 24);
        } else {
            v[s] = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        }
    }
    uint32_t l, r;
    if (_slots == 1) {
        l = (_mask & I2S_STD_SLOT_LEFT) ? v[0] : 0u;
        r = (_mask & I2S_STD_SLOT_RIGHT) ? v[0] : 0u;
    } else {
        l = v[0];
        r = v[1];
    }
    if (_inv_ws) {
        const uint32_t t = l;
        l = r;
        r = t;
    }
    if (_bits == 16) {
        out[0] = (uint16_t)(l >> 16);
        out[1] = (uint16_t)(r >> 16);
    } else if (_bits == 24 && _format == CH32RVI2SFormat::Lsb) {
        out[0] = (uint16_t)(l >> 24);
        out[1] = (uint16_t)(l >> 8);
        out[2] = (uint16_t)(r >> 24);
        out[3] = (uint16_t)(r >> 8);
    } else {
        out[0] = (uint16_t)(l >> 16);
        out[1] = (uint16_t)l;
        out[2] = (uint16_t)(r >> 16);
        out[3] = (uint16_t)r;
    }
}

void I2SClass::unpackFrame(const uint16_t *in, uint8_t *out) const
{
    if (_sai >= 0) {
        const uint8_t bytes = (uint8_t)(_bits / 8u);
        const uint8_t per = _bits == 16 ? 1u : 2u;
        const uint8_t first = (!_tdm_mask && _slots == 1 && !(_mask & I2S_STD_SLOT_LEFT)) ? 1u : 0u;
        const uint8_t count = (_tdm_mask || _slots == 2) ? _hw_slots : 1u;
        for (uint8_t k = 0; k < count; k++) {
            const uint16_t *w = in + (first + k) * per;
            uint32_t v = _bits == 16 ? (uint32_t)(int32_t)(int16_t)w[0] : (uint32_t)w[0] | (uint32_t)w[1] << 16;
            for (uint8_t b = 0; b < bytes; b++) {
                out[k * bytes + b] = (uint8_t)(v >> (8u * b));
            }
        }
        return;
    }
    uint32_t l, r;
    if (_bits == 16) {
        l = (uint32_t)in[0] << 16;
        r = (uint32_t)in[1] << 16;
    } else if (_bits == 24 && _format == CH32RVI2SFormat::Lsb) {
        l = ((uint32_t)(in[0] & 0xFFu) << 24) | ((uint32_t)in[1] << 8);
        r = ((uint32_t)(in[2] & 0xFFu) << 24) | ((uint32_t)in[3] << 8);
    } else {
        l = ((uint32_t)in[0] << 16) | in[1];
        r = ((uint32_t)in[2] << 16) | in[3];
    }
    if (_inv_ws) {
        const uint32_t t = l;
        l = r;
        r = t;
    }
    uint32_t v[2] = {l, r};
    if (_slots == 1) {
        v[0] = (_mask == I2S_STD_SLOT_RIGHT) ? r : l;
    }
    for (uint8_t s = 0; s < _slots; s++) {
        uint8_t *b = out + s * (_bits / 8u);
        if (_bits == 16) {
            b[0] = (uint8_t)(v[s] >> 16);
            b[1] = (uint8_t)(v[s] >> 24);
        } else if (_bits == 24) {
            b[0] = (uint8_t)(v[s] >> 8);
            b[1] = (uint8_t)(v[s] >> 16);
            b[2] = (uint8_t)(v[s] >> 24);
        } else {
            b[0] = (uint8_t)v[s];
            b[1] = (uint8_t)(v[s] >> 8);
            b[2] = (uint8_t)(v[s] >> 16);
            b[3] = (uint8_t)(v[s] >> 24);
        }
    }
}

/* ---------------------------------------------------------------- output */

size_t I2SClass::write(uint8_t b)
{
    return write(&b, 1);
}

size_t I2SClass::write(const uint8_t *buffer, size_t size)
{
    if (_port < 0 || !_tx) {
        _error = I2S_ERROR_STATE;
        return 0;
    }
    const size_t fb = frameBytes();
    const uint32_t hpf = (uint32_t)hwPerFrame();
    size_t done = 0;
    while (done < size) {
        const uint8_t *frame;
        size_t frames;
        if (_staged || size - done < fb) {
            /* a frame in pieces */
            while (_staged < fb && done < size) {
                _stage[_staged++] = buffer[done++];
            }
            if (_staged < fb) {
                break;
            }
            frame = _stage;
            frames = 1;
        } else {
            frame = buffer + done;
            frames = (size - done) / fb;
            if (frames > 32u) {
                frames = 32u;   /* interrupts off for 32 frames at most */
            }
        }
        size_t put = 0;
        while (put < frames) {
            const uint32_t irq = ch32rv_irq_save();
            const uint32_t room = (_half - _w_ofs) / hpf;
            if (room) {
                uint32_t n = (uint32_t)(frames - put);
                if (n > room) {
                    n = room;
                }
                uint16_t *out = _ring + (uint32_t)_w_half * _half + _w_ofs;
                for (uint32_t k = 0; k < n; k++) {
                    packFrame(frame + (put + k) * fb, out + k * hpf);
                }
                _w_ofs += n * hpf;
                _fill[_w_half] += n * hpf;
                put += n;
            }
            ch32rv_irq_restore(irq);
            if (!room) {
                yield();   /* both halves full: until the DMA frees one */
            }
        }
        if (frame == _stage) {
            _staged = 0;
        } else {
            done += frames * fb;
        }
    }
    return done;
}

int I2SClass::availableForWrite()
{
    if (_port < 0 || !_tx) {
        return 0;
    }
    const size_t frames = (_half - _w_ofs) / hwPerFrame();
    const size_t bytes = frames * frameBytes();
    return (int)(bytes > _staged ? bytes - _staged : 0);
}

void I2SClass::flush()
{
    if (_port < 0 || !_tx) {
        return;
    }
    /* What is written plays: the half being filled counts as full (the rest
     * of it is the silence it was cleared to), then both halves out. */
    uint32_t irq = ch32rv_irq_save();
    if (_w_ofs) {
        _w_ofs = _half;
    }
    ch32rv_irq_restore(irq);
    while (_fill[0] || _fill[1]) {
        yield();
    }
}

/* ----------------------------------------------------------------- input */

/* Halfwords the sketch can take: the halves the DMA finished and the sketch
 * has not read, of which only the last is still whole - the DMA writes over
 * the one before while filling the next. */
size_t I2SClass::rxReady()
{
    const uint32_t halves = _rx_halves;
    const uint32_t behind = halves - _rx_read;
    if (behind == 0u) {
        return 0;
    }
    if (behind > 1u) {
        _overruns += behind - 1u;
        _rx_read = halves - 1u;
        _rx_off = 0;
        _peeked = _peek_pos = 0;
    }
    return _half - _rx_off;
}

/* One frame from the ring into _peek, false when there is none. */
bool I2SClass::fill()
{
    if (_pdm_on) {
        return _pdm_ops->fill(*this);
    }
    if (rxReady() == 0) {
        return false;
    }
    dma_fence();
    unpackFrame(_ring + (_rx_read & 1u) * _half + _rx_off, _peek);
    _rx_off += (uint32_t)hwPerFrame();
    if (_rx_off >= _half) {
        _rx_off = 0;
        _rx_read++;
    }
    _peeked = (uint8_t)frameBytes();
    _peek_pos = 0;
    return true;
}

int I2SClass::available()
{
    if (!rxBegun()) {
        return 0;
    }
    if (_pdm_on) {
        return (int)((_peeked - _peek_pos) + _pdm_ops->frames(*this) * frameBytes());
    }
    return (int)((_peeked - _peek_pos) + rxReady() / hwPerFrame() * frameBytes());
}

int I2SClass::read()
{
    if (!rxBegun()) {
        return -1;
    }
    if (_peek_pos >= _peeked && !fill()) {
        return -1;
    }
    return _peek[_peek_pos++];
}

int I2SClass::peek()
{
    if (!rxBegun()) {
        return -1;
    }
    if (_peek_pos >= _peeked && !fill()) {
        return -1;
    }
    return _peek[_peek_pos];
}

/* Waits up to the stream's timeout (setTimeout(), 1 s by default) for
 * length bytes, as Stream's does; whole frames go straight across. */
size_t I2SClass::readBytes(char *buffer, size_t length)
{
    if (!rxBegun()) {
        _error = I2S_ERROR_STATE;
        return 0;
    }
    uint8_t *out = (uint8_t *)buffer;
    if (_pdm_on) {
        size_t done = 0;
        const unsigned long start = millis();
        while (done < length) {
            if (_peek_pos >= _peeked && !fill()) {
                if (millis() - start >= _timeout) {
                    break;
                }
                yield();
                continue;
            }
            out[done++] = _peek[_peek_pos++];
        }
        return done;
    }
    const size_t fb = frameBytes();
    const uint32_t hpf = (uint32_t)hwPerFrame();
    size_t done = 0;
    const unsigned long start = millis();
    while (done < length) {
        while (_peek_pos < _peeked && done < length) {
            out[done++] = _peek[_peek_pos++];
        }
        if (done == length) {
            break;
        }
        size_t ready = rxReady() / hpf;
        if (ready == 0) {
            if (millis() - start >= _timeout) {
                break;
            }
            yield();
            continue;
        }
        dma_fence();
        while (ready && length - done >= fb) {
            unpackFrame(_ring + (_rx_read & 1u) * _half + _rx_off, out + done);
            done += fb;
            ready--;
            _rx_off += hpf;
            if (_rx_off >= _half) {
                _rx_off = 0;
                _rx_read++;
            }
        }
        if (ready && done < length) {
            fill();   /* the part of a frame that is left */
        }
    }
    return done;
}

/* --------------------------------------------------------- the interrupt */

void I2SClass::dmaInterrupt()
{
    if (_port < 0 || !_dma) {
        return;
    }
    const ch32rv_dma_line_t &line = *static_cast<const ch32rv_dma_line_t *>(_dma);
    const uint32_t f = (CH32RV_DMA_INTFR(&line) >> line.shift) & 0xFu;
    CH32RV_DMA_INTFCR(&line) = f << line.shift;
    /* half 0 done at HT, half 1 at TC; both at once when this came late */
    for (uint8_t h = 0; h < 2; h++) {
        if (f & (h ? CH32RV_DMA_TCIF : CH32RV_DMA_HTIF)) {
            halfDone(h);
        }
    }
}

/* Half h of the ring moved: an input's to read, an output's to fill again. */
void I2SClass::halfDone(uint8_t h)
{
    if (!_tx) {
        _rx_halves = _rx_halves + 1u;
        return;
    }
    const uint8_t next = (uint8_t)(1u - h);
    const uint32_t had = _fill[h];
    _playing = next;
    memset(_ring + (uint32_t)h * _half, 0, _half * sizeof(uint16_t));
    _fill[h] = 0;
    if (_w_half == next) {
        /* the half write() was on plays now: write() moves to the one just
         * freed; short of full while sound was flowing, it is an underrun */
        if (had && _w_ofs < _half) {
            _underruns = _underruns + 1u;
        }
        _w_half = h;
        _w_ofs = 0;
    }
}

namespace {
inline void port_irq(uint8_t i)
{
    if (owners[i]) {
        owners[i]->dmaInterrupt();
    }
}
}   // namespace

/* Each SPI's two DMA vectors; only the direction an instance runs is
 * enabled. Table order: I2S2 first where it exists. */
#ifdef CH32RV_I2S2_PADS
extern "C" CH32RV_INTERRUPT void I2S_CAT(CH32RV_SPI2_DMA_TX_IRQ, _IRQHandler)(void)
{
    port_irq(0);
}
extern "C" CH32RV_INTERRUPT void I2S_CAT(CH32RV_SPI2_DMA_RX_IRQ, _IRQHandler)(void)
{
    port_irq(0);
}
#define I2S3_INDEX 1
#else
#define I2S3_INDEX 0
#endif
#ifdef CH32RV_I2S3_PADS
extern "C" CH32RV_INTERRUPT void I2S_CAT(CH32RV_SPI3_DMA_TX_IRQ, _IRQHandler)(void)
{
    port_irq(I2S3_INDEX);
}
extern "C" CH32RV_INTERRUPT void I2S_CAT(CH32RV_SPI3_DMA_RX_IRQ, _IRQHandler)(void)
{
    port_irq(I2S3_INDEX);
}
#endif

/* ------------------------------------------------------------------ SAI */

#ifdef CH32RV_SAI_PADS
/* CH32H417's SAI (EVT ch32h417.h, the STM32 SAI's registers): block A at
 * 0x40015804, B at 0x40015824 - CFGR1, CFGR2, FRCR, SLOTR, INTENR, SR, -,
 * DATAR. Its clock SAI_CK = SYSCLK / 6 and MCLK = SAI_CK / MCKDIV = 256 fs
 * (EVT SAI_Play's formula: RM 36.3.1's "/ (MCKDIV x 2)" made every rate
 * twice what was asked, H417 2026-11-23). The FIFO's request interrupt
 * (FREQ, at half the FIFO) moves the ring's halfwords / words to and from
 * DATAR, as many as the FIFO level (SR.FLTH) allows, and runs halfDone() at
 * each half - the same ring the SPI's DMA uses; the SD bit stream decoded
 * (WireSkein spi, measured) carried every item of the ring once, in order
 * (H417, 2026-11-23). No DMA channel taken: one interrupt for both blocks. */
namespace {
#define SAI_REG(b, off) CH32RV_REG32(0x40015804u + 0x20u * (b) + (off))
#define SAI_CFGR1(b)  SAI_REG(b, 0x00u)
#define SAI_CFGR2(b)  SAI_REG(b, 0x04u)
#define SAI_FRCR(b)   SAI_REG(b, 0x08u)
#define SAI_SLOTR(b)  SAI_REG(b, 0x0Cu)
#define SAI_INTENR(b) SAI_REG(b, 0x10u)
#define SAI_SR(b)     SAI_REG(b, 0x14u)
#define SAI_DATAR(b)  SAI_REG(b, 0x1Cu)
constexpr uint32_t SAI_EN = 1u << 16, SAI_CKSTR = 1u << 9, SAI_FREQ = 1u << 3, SAI_OVRUDR = 1u << 0;
constexpr uint32_t SAI_CK_HZ = CH32RV_CLOCK_SYSCLK_HZ / 6u;
const Pad sai_pads[] = CH32RV_SAI_PADS;
I2SClass *sai_owners[2];

const Pad *sai_pad(int pin, uint8_t role)
{
    for (const Pad &q : sai_pads) {
        if (q.role == role && q.pad == (uint8_t)pin) {
            return &q;
        }
    }
    return nullptr;
}

void sai_irq(void *)
{
    for (I2SClass *o : sai_owners) {
        if (o) {
            o->saiInterrupt();
        }
    }
}
}   // namespace

/* IEC 60958-3 consumer channel status, bits 0 .. 31: PCM audio, copying
 * permitted (bit 2), category general, the sampling frequency (bits 24 ..
 * 27, bit 24 first; "not indicated" for a rate that has no code). */
static uint32_t spdif_status(uint32_t rate)
{
    static const struct { uint32_t hz; uint8_t code; } codes[] = {
        {44100, 0x0}, {48000, 0x2}, {32000, 0x3}, {88200, 0x8}, {96000, 0xA}, {176400, 0xC}, {192000, 0xE},
    };
    uint8_t code = 0x1;   /* not indicated */
    for (const auto &c : codes) {
        if (c.hz == rate) {
            code = c.code;
        }
    }
    return 1u << 2 | (uint32_t)code << 24;
}

bool I2SClass::beginSai(bool tx, uint32_t rate, bool tdm, bool spdif)
{
    _error = I2S_ERROR_PINS;
    const int data = tx ? _dout : _din;
    int8_t b = -1;
    for (uint8_t k = 0; k < 2 && b < 0; k++) {
        /* SPDIF: the SD pad alone, the block's other pins released (RM 36.3.8) */
        if (!sai_owners[k] && sai_pad(data, (uint8_t)(4 * k + 2)) &&
            (spdif ? (_bclk < 0 && _ws < 0 && _mclk < 0)
                   : (sai_pad(_bclk, (uint8_t)(4 * k)) && (_ws < 0 || sai_pad(_ws, (uint8_t)(4 * k + 1))) &&
                      (_mclk < 0 || sai_pad(_mclk, (uint8_t)(4 * k + 3)))))) {
            b = (int8_t)k;
        }
    }
    if (b < 0) {
        return false;
    }
    /* The frame: two slots (STD) or up to the mask's highest rounded up to
     * 2, 4 or 8 (TDM), of 16 bits for 16-bit samples and 32 for 24 / 32;
     * 256 bits at most (MCLK, 256 fs, over SCK has to be whole). */
    _error = I2S_ERROR_ARG;
    const uint8_t slot_bits = _bits == 16 ? 16u : 32u;
    uint8_t nslots = 2;
    uint16_t enable = 0x3u;
    if (tdm) {
        const uint8_t top = (uint8_t)(32 - __builtin_clz((uint32_t)_tdm_mask));
        while (nslots < top) {
            nslots = (uint8_t)(nslots * 2u);
        }
        enable = _tdm_mask;
        _slots = (uint8_t)__builtin_popcount(_tdm_mask);
    }
    const uint32_t frame = (uint32_t)nslots * slot_bits;
    if (frame > 256u) {
        return false;
    }
    _hw_slots = tdm ? _slots : 2u;
    /* STD / TDM: MCLK = SAI_CK / div = 256 fs. SPDIF: NODIV, the bit clock
     * SAI_CK / div itself = 128 fs (two cells a bit, 64 bits a frame) - with
     * the MCLK divider the cells came 16 times too slow (and 32 with 16-bit
     * samples), H417 2026-11-24, WireSkein's spdif. */
    const uint32_t per = spdif ? 128u : 256u;
    const uint32_t div = (uint32_t)((SAI_CK_HZ + per / 2u * rate) / ((uint64_t)per * rate));   /* nearest */
    if (div < 1u || div > 63u) {
        return false;
    }
    const uint32_t hpf = hwPerFrame();
    _half = (uint32_t)(_half_bytes / 2u / hpf * hpf);
    if (_half < hpf) {
        _half = hpf;
    }
    _ring = (uint16_t *)calloc(2u * _half, sizeof(uint16_t));
    _error = I2S_ERROR_MEMORY;
    if (!_ring) {
        return false;
    }
    _error = I2S_ERROR_STATE;
    if (!sai_owners[b ^ 1] && !ch32rv_irq_attach(CH32RV_IRQN_SAI, sai_irq, nullptr)) {
        free(_ring);
        _ring = nullptr;
        return false;
    }
    _sai = b;
    _spdif = spdif;
    _spdif_sub = 0;
    _spdif_cs = spdif ? spdif_status(rate) : 0u;
    _dma = nullptr;
    _tx = tx;
    _port = (int8_t)(PORT_COUNT + b);
    _sai_pos = 0;
    _rate = (uint32_t)(SAI_CK_HZ / ((uint64_t)per * div));
    _playing = 0;
    _w_half = 1;
    _w_ofs = 0;
    _fill[0] = _fill[1] = 0;
    _underruns = _overruns = 0;
    _staged = 0;
    _rx_halves = 0;
    _rx_read = 0;
    _rx_off = 0;
    _peeked = _peek_pos = 0;

    CH32RV_REG32(0x4002101Cu) |= 1u << 16;   /* RCC HB2PCENR SAIEN (both blocks) */
    const int pins[4] = {_bclk, _ws, data, _mclk};
    for (uint8_t r = 0; r < 4; r++) {
        if (pins[r] >= 0) {
            ch32rv_pin_set_af((uint8_t)pins[r], sai_pad(pins[r], (uint8_t)(4 * b + r))->value);
            pad_mode(pins[r], (r == 2 && !tx) ? CH32RV_GPIO_CFG_IN_FLOAT : CH32RV_GPIO_CFG_AF_PP_50M);
        }
    }
    /* The frame sync per format: Philips FS low for the first (left) half,
     * one bit early; MSB / LSB justified high for the left half, on the
     * first bit (LSB: the word at the slot's end); PCM a pulse (1 or 13 bits)
     * starting the frame, one bit early for the short one. TDM's Philips FS
     * low for the first half of the frame. */
    uint32_t fsall = frame / 2u - 1u, fsdef = tdm ? 0u : 1u, fspol = 0, fsoff = 1, fboff = 0;
    switch (_format) {
    case CH32RVI2SFormat::Msb:
        fspol = 1;
        fsoff = 0;
        break;
    case CH32RVI2SFormat::Lsb:
        fspol = 1;
        fsoff = 0;
        fboff = slot_bits - _bits;
        break;
    case CH32RVI2SFormat::PcmShort:
        fsall = 0;
        fsdef = 0;
        fspol = 1;
        break;
    case CH32RVI2SFormat::PcmLong:
        fsall = 12;
        fsdef = 0;
        fspol = 1;
        fsoff = 0;
        break;
    default:
        break;
    }
    if (_inv_ws) {
        fspol ^= 1u;
    }
    static const uint8_t ds_code[] = {4, 6, 7};   /* 16, 24, 32 bits */
    SAI_CFGR1(b) = 0;
    SAI_CFGR2(b) = 1u << 3;   /* FIFO flushed */
    SAI_CFGR2(b) = 2u;        /* FREQ at half the FIFO */
    SAI_FRCR(b) = (frame - 1u) | fsall << 8 | fsdef << 16 | fspol << 17 | fsoff << 18;
    SAI_SLOTR(b) = fboff | (slot_bits == 16 ? 1u : 2u) << 6 | (uint32_t)(nslots - 1u) << 8 | (uint32_t)enable << 16;
    if (spdif) {
        /* PRTCFG 01: SPDIF, master transmitter; the frame is two 32-bit
         * subframes whatever FRCR / SLOTR say */
        SAI_FRCR(b) = 0;
        SAI_SLOTR(b) = 0;
        SAI_CFGR1(b) = 1u << 2 | 6u << 5 | 1u << 19 | div << 20;   /* PRTCFG SPDIF, DS 24, NODIV */
    } else {
        SAI_CFGR1(b) = (tx ? 0u : 1u) | (uint32_t)ds_code[_bits == 16 ? 0 : _bits == 24 ? 1 : 2] << 5 |
                       (_inv_bclk ? 0u : SAI_CKSTR) | div << 20;
    }
    sai_owners[b] = this;
    if (tx) {
        saiInterrupt();   /* the FIFO full of the (silent) ring before the clock starts */
    }
    SAI_INTENR(b) = SAI_FREQ;
    SAI_CFGR1(b) |= SAI_EN;
    _error = I2S_ERROR_NONE;
    return true;
}

/* FREQ: an output's FIFO below half, or an input's above. Items move until
 * the FIFO is full / empty (SR.FLTH: 0 empty .. 5 full), each half of the
 * ring finished running halfDone(). */
void I2SClass::saiInterrupt()
{
    const uint8_t b = (uint8_t)_sai;
    if (SAI_SR(b) & SAI_OVRUDR) {
        SAI_REG(b, 0x18u) = SAI_OVRUDR;   /* CLRFR */
        if (!_tx) {
            _overruns = _overruns + 1u;
        }
    }
    const bool wide = _bits != 16;
    const uint32_t end = 2u * _half;
    uint32_t level = (SAI_SR(b) >> 16) & 7u;
    for (uint8_t guard = 0; guard < 16u && (_tx ? level < 5u : level != 0u); guard++) {
        uint16_t *at = _ring + _sai_pos;
        if (_spdif) {
            /* DATAR[23:0] the sample's 24 high bits (MSB at 23), [26] the
             * channel status bit of this frame, [24] valid 0, and [25] the
             * user bit equal to it: the SAI's parity covers DATAR[23:0] only
             * (RM 36.3.8 says [26:0]; measured on H417 2026-11-24 - V, U or
             * C alone set left the subframe's parity wrong, U and C together
             * right), so V, U and C hold an even number of ones. */
            const uint32_t v = wide ? ((uint32_t)at[0] | (uint32_t)at[1] << 16) : (uint32_t)at[0] << 16;
            const uint32_t audio = _bits == 24 ? (v & 0xFFFFFFu) : (v >> 8);
            const uint32_t frame = _spdif_sub >> 1;
            const uint32_t c = frame < 32u ? (_spdif_cs >> frame) & 1u : 0u;
            SAI_DATAR(b) = audio | c << 26 | c << 25;
            _spdif_sub = (uint16_t)(_spdif_sub + 1u == 384u ? 0u : _spdif_sub + 1u);
        } else if (_tx) {
            SAI_DATAR(b) = wide ? ((uint32_t)at[0] | (uint32_t)at[1] << 16) : (uint32_t)(int32_t)(int16_t)at[0];
        } else {
            const uint32_t v = SAI_DATAR(b);
            at[0] = (uint16_t)v;
            if (wide) {
                at[1] = (uint16_t)(v >> 16);
            }
        }
        level = (SAI_SR(b) >> 16) & 7u;
        _sai_pos += wide ? 2u : 1u;
        if (_sai_pos == _half) {
            halfDone(0);
        } else if (_sai_pos >= end) {
            _sai_pos = 0;
            halfDone(1);
        }
    }
}

void I2SClass::endSai()
{
    const uint8_t b = (uint8_t)_sai;
    SAI_INTENR(b) = 0;
    SAI_CFGR1(b) &= ~SAI_EN;
    for (uint32_t t = 0; t < 100000u && (SAI_CFGR1(b) & SAI_EN); t++) {
    }
    SAI_CFGR1(b) = 0;
    sai_owners[b] = nullptr;
    if (!sai_owners[b ^ 1]) {
        ch32rv_irq_detach(CH32RV_IRQN_SAI);
        CH32RV_REG32(0x40021010u) |= 1u << 16;   /* RCC HB2PRSTR SAIRST */
        CH32RV_REG32(0x40021010u) &= ~(1u << 16);
        CH32RV_REG32(0x4002101Cu) &= ~(1u << 16);
    }
    pad_mode(_bclk, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_ws, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_tx ? _dout : _din, CH32RV_GPIO_CFG_IN_FLOAT);
    pad_mode(_mclk, CH32RV_GPIO_CFG_IN_FLOAT);
    _sai = -1;
    _spdif = false;
    _port = -1;
    free(_ring);
    _ring = nullptr;
    _rate = 0;
}
#else
bool I2SClass::beginSai(bool, uint32_t, bool, bool)
{
    _error = I2S_ERROR_PINS;
    return false;
}

void I2SClass::endSai() {}
void I2SClass::saiInterrupt() {}
#endif

#endif   // CH32RV_HAS_I2S
