#include "CH32RVDma.h"

#include <ch32rv_priority.h>

#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_irq.h"

#if !defined(CH32RV_DMA_CHANNELS)
#error "CH32RVDma: this series has no DMA channel table (CH32RV_DMA_CHANNELS in its variant)"
#endif

namespace {

struct Channel {
    ch32rv_dma_line_t line;
    uint16_t irqn;
};

#define CH32RV_DMA_ENTRY(d, c, irq, regs, flags, shift) {CH32RV_DMA_LINE(d, c, regs, flags, shift, 0), (uint16_t)(irq)},
const Channel channels[] = {CH32RV_DMA_CHANNELS(CH32RV_DMA_ENTRY)};
#undef CH32RV_DMA_ENTRY
constexpr int channelCount = (int)(sizeof channels / sizeof channels[0]);

constexpr uint32_t CFGR_PINC = 1u << 6;
constexpr uint32_t CFGR_PSIZE_16 = 1u << 8;
constexpr uint32_t CFGR_MSIZE_16 = 1u << 10;
constexpr uint32_t CFGR_MEM2MEM = 1u << 14;
constexpr size_t PART = 65535u;     /* CNTR's 16 bits */

void onInterrupt(void *self)
{
    static_cast<CH32RVDma *>(self)->service();
}

}  // namespace

bool CH32RVDma::begin()
{
    if (isAttached()) {
        return true;
    }
    for (int i = channelCount - 1; i >= 0; i--) {
        const Channel &c = channels[i];
        if (!ch32rv_dma_claim(c.line.dma, c.line.channel)) {
            continue;
        }
        if (!ch32rv_irq_attach(c.irqn, onInterrupt, this)) {
            ch32rv_dma_release(c.line.dma, c.line.channel);
            continue;               /* a handler of its own is linked: another user's channel */
        }
        ch32rv_dma_open(&c.line);
        _index = (int8_t)i;
        return true;
    }
    return false;
}

void CH32RVDma::end()
{
    if (!isAttached()) {
        return;
    }
    abort();
    const Channel &c = channels[_index];
    ch32rv_irq_detach(c.irqn);
    ch32rv_dma_close(&c.line);
    ch32rv_dma_release(c.line.dma, c.line.channel);
    _index = -1;
}

uint8_t CH32RVDma::dma() const
{
    return isAttached() ? channels[_index].line.dma : 0u;
}

uint8_t CH32RVDma::channel() const
{
    return isAttached() ? channels[_index].line.channel : 0u;
}

void CH32RVDma::onDone(Callback cb, void *arg)
{
    const uint32_t irq = ch32rv_irq_save();
    _cb = cb;
    _arg = arg;
    ch32rv_irq_restore(irq);
}

bool CH32RVDma::start(void *dst, const void *src, size_t bytes, bool fill, uint8_t value)
{
    if (!isAttached() || busy() || bytes == 0u) {
        return false;
    }
    _pattern = 0x01010101u * value;
    const uint32_t d = (uint32_t)(uintptr_t)dst, s = (uint32_t)(uintptr_t)src;
    const uint32_t mix = d | (fill ? 0u : s) | (uint32_t)bytes;
    _unit = (mix & 3u) == 0u ? 4u : (mix & 1u) == 0u ? 2u : 1u;
    _dst = d;
    _src = s;
    _fill = fill;
    _left = bytes / _unit;
    next();
    return true;
}

/* The next part onto the channel (from the thread at the start, then from the interrupt). */
void CH32RVDma::next()
{
    const ch32rv_dma_line_t *l = &channels[_index].line;
    const size_t units = _left > PART ? PART : _left;
    _left = _left - units;
    const uint32_t size = _unit == 4u ? CH32RV_DMA_CFGR_PSIZE_32 | CH32RV_DMA_CFGR_MSIZE_32
                        : _unit == 2u ? CFGR_PSIZE_16 | CFGR_MSIZE_16 : 0u;
    CH32RV_DMA_CFGR(l) = 0;
    CH32RV_DMA_INTFCR(l) = 0xFu << l->shift;
    /* The read side is the "peripheral" one (DIR 0: peripheral to memory); a fill reads one word, not moving. */
    CH32RV_DMA_PADDR(l) = _fill ? (uint32_t)(uintptr_t)&_pattern : _src;
    CH32RV_DMA_MADDR(l) = _dst;
    CH32RV_DMA_CNTR(l) = (uint32_t)units;
    _dst += (uint32_t)units * _unit;
    if (!_fill) {
        _src += (uint32_t)units * _unit;
    }
    _running = true;
    __asm__ volatile ("fence rw, rw" ::: "memory");      /* the source written before the channel reads it */
    CH32RV_DMA_CFGR(l) = CFGR_MEM2MEM | CH32RV_DMA_CFGR_MINC | (_fill ? 0u : CFGR_PINC) | size |
                         CH32RV_DMA_CFGR_PL(1) | CH32RV_DMA_CFGR_TCIE | CH32RV_DMA_CFGR_EN;
}

void CH32RVDma::service()
{
    if (!isAttached()) {
        return;
    }
    const ch32rv_dma_line_t *l = &channels[_index].line;
    if (!_running || !((CH32RV_DMA_INTFR(l) >> l->shift) & CH32RV_DMA_TCIF)) {
        return;
    }
    CH32RV_DMA_CFGR(l) = 0;
    CH32RV_DMA_INTFCR(l) = 0xFu << l->shift;
    if (_left != 0u) {
        next();
        return;
    }
    _running = false;
    __asm__ volatile ("fence rw, rw" ::: "memory");      /* the copy seen before the job is reported done */
    if (_cb) {
        _cb(_arg);
    }
}

void CH32RVDma::wait()
{
    while (busy()) {
        /* With interrupts masked the channel's interrupt never runs: drive the job from here. */
        const uint32_t irq = ch32rv_irq_save();
        service();
        ch32rv_irq_restore(irq);
    }
}

void CH32RVDma::abort()
{
    if (!isAttached()) {
        return;
    }
    const uint32_t irq = ch32rv_irq_save();
    const ch32rv_dma_line_t *l = &channels[_index].line;
    CH32RV_DMA_CFGR(l) = 0;
    CH32RV_DMA_INTFCR(l) = 0xFu << l->shift;
    _left = 0;
    _running = false;
    ch32rv_irq_restore(irq);
}

bool CH32RVDma::copyAsync(void *dst, const void *src, size_t bytes)
{
    return start(dst, src, bytes, false, 0u);
}

bool CH32RVDma::fillAsync(void *dst, uint8_t value, size_t bytes)
{
    return start(dst, nullptr, bytes, true, value);
}

/* The waiting forms: onDone()'s callback is for the background jobs, so it is held back meanwhile. */
bool CH32RVDma::copy(void *dst, const void *src, size_t bytes)
{
    return run(dst, src, bytes, false, 0u);
}

bool CH32RVDma::fill(void *dst, uint8_t value, size_t bytes)
{
    return run(dst, nullptr, bytes, true, value);
}

bool CH32RVDma::run(void *dst, const void *src, size_t bytes, bool fill, uint8_t value)
{
    if (busy()) {
        return false;
    }
    const Callback cb = _cb;
    _cb = nullptr;
    const bool ok = start(dst, src, bytes, fill, value);
    wait();
    _cb = cb;
    return ok;
}
