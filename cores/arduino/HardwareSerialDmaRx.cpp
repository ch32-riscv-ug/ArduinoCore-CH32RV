/* setRxBufferSize() (HardwareSerial.h): the port's receive buffer filled by
 * DMA. HardwareSerialDmaTx.cpp is the transmit side.
 *
 * In a file of its own, reached from HardwareSerial.cpp and the modes only
 * through ch32rv_serial_dma_rx, which only this file writes: a sketch that
 * does not call setRxBufferSize() links none of it, and every test of a
 * port's receive on DMA folds away (as the modes' pointer,
 * HardwareSerialMode.cpp).
 *
 * One channel in circular mode from the data register into the buffer,
 * started at begin() and not stopped until end(): the bytes keep coming in
 * whatever the CPU does, interrupts off included. The reader takes the DMA's
 * place from its count (CNTR) - available() (the count alone between the
 * DMA's half and end points), and peek() / read() once the bytes of the last
 * look are read, onReceive()'s delivery after loop() - with no interrupt per byte and none from the DMA (its
 * vectors stay the libraries': CH32RVAdcStream, CH32RVLogicCapture).
 *
 * The DMA cannot be refused a byte, so an overrun is noticed instead: at a
 * look the DMA has gone past unread bytes, or its half-transfer /
 * transfer-complete flag (HTIF / TCIF, read and cleared at every look) says
 * it reached the half or the end more often than its move since the last
 * look accounts for. The oldest bytes are lost then; reading goes on from
 * the oldest still there and UART_BUFFER_FULL_ERROR is reported. A whole
 * buffer more between two looks that passes both points again goes unseen.
 * The USART's error flags (ORE, NE, FE, PE) are read at each look and
 * reported when they newly rise; the byte stays (the DMA does not know). */
#include "HardwareSerial.h"

#include "Arduino.h"
#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_serial_dma.h"
#include "internal/ch32rv_serial_irq.h"

#include <stdlib.h>

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

namespace {

/* The ADC's runs claim their channel from now on (internal/ch32rv_dma.h). */
const bool claims_linked = (ch32rv_dma_claims_link(), true);

const ch32rv_dma_line_t lines[] = CH32RV_SERIAL_DMA_LINES(rx);

struct Rx {
    uint8_t *buf;
    uint16_t size;          /* 0: setRxBufferSize() not called           */
    uint8_t own;            /* buf is from the heap                      */
    uint8_t seen;           /* the USART's error flags at the last look  */
    uint16_t tail;          /* the reader's place                        */
    uint16_t last;          /* the DMA's place at the last look          */
    uint16_t cap;           /* own: the heap block's size                */
};
Rx ports[CH32RV_SERIAL_COUNT];

/* free(), once setRxBufferSize() took the heap: a later size the built-in
 * storage holds gives the heap buffer back without linking the heap into a
 * sketch that never used it. */
void (*heap_free)(void *);

}  // namespace

namespace arduino {

struct CH32RVSerialDmaRx {
    static Rx &of(CH32RVHardwareSerial &port) { return ports[CH32RV_SERIAL_COUNT == 1 ? 0 : port._number]; }
    static const ch32rv_dma_line_t *line(CH32RVHardwareSerial &port) { return &lines[CH32RV_SERIAL_COUNT == 1 ? 0 : port._slot - 1u]; }

    /* The DMA's place: the next byte goes there. */
    static uint32_t place(uint32_t n, uint32_t count)
    {
        return count == 0u || count >= n ? 0u : n - count;
    }

    static uint16_t position(CH32RVHardwareSerial &port)
    {
        return (uint16_t)place(of(port).size, CH32RV_DMA_CNTR(line(port)));
    }

    /* A look at the DMA: its place, an overrun since the last look, the
     * USART's errors. Returns the bytes to read. The reader's side only. */
    static uint32_t look(CH32RVHardwareSerial &port)
    {
        Rx &r = of(port);
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t n = r.size;
        constexpr uint32_t points = CH32RV_DMA_HTIF | CH32RV_DMA_TCIF;
        /* The flags and the count of one moment: a flag that rose between
         * the two reads is taken with a count read after it. */
        uint32_t flags = (CH32RV_DMA_INTFR(l) >> l->shift) & points;
        uint32_t count = CH32RV_DMA_CNTR(l);
        const uint32_t again = (CH32RV_DMA_INTFR(l) >> l->shift) & points;
        if (again != flags) {
            count = CH32RV_DMA_CNTR(l);
            flags = again;
        }
        if (flags) {
            CH32RV_DMA_INTFCR(l) = flags << l->shift;
        }
        /* The DMA wrote memory the compiler knows nothing about: the bytes
         * up to this count are read after it (CH32RVHsAdc's DMA_DONE). */
        __asm__ volatile("fence rw, rw" ::: "memory");
        const uint32_t pos = place(n, count);
        const uint32_t last = r.last, tail = r.tail;
        const uint32_t moved = pos >= last ? pos - last : pos + n - last;
        const uint32_t unread = last >= tail ? last - tail : last + n - tail;
        /* The places reached since: (last, last + moved]. HTIF rises on
         * reaching n / 2, TCIF on reaching n (0 again). */
        const uint32_t reach = last + moved;
        const uint32_t half = n >> 1;
        uint32_t marks = 0;
        if (unread + moved >= n || ((flags & CH32RV_DMA_TCIF) && reach < n) ||
            ((flags & CH32RV_DMA_HTIF) && reach < (last < half ? half : n + half))) {
            marks = CH32RV_SERIAL_MARK_FULL;
            r.tail = (uint16_t)ch32rv_serial_dma_wrap(pos + 1u, n);   /* the oldest byte still there */
        }
        r.last = (uint16_t)pos;
        const uint32_t status = CH32RV_USART_STATR(port.registers()) &
                                (CH32RV_USART_STATR_ORE | CH32RV_USART_STATR_NE | CH32RV_USART_STATR_FE |
                                 CH32RV_USART_STATR_PE);
        uint32_t fresh = status & ~(uint32_t)r.seen;
        r.seen = (uint8_t)status;
        if ((fresh & CH32RV_USART_STATR_FE) && !r.buf[(pos ? pos : n) - 1u]) {
            fresh ^= CH32RV_USART_STATR_FE | CH32RV_SERIAL_MARK_BREAK;   /* the line held low */
        }
        marks |= fresh;
        if (marks) {
            const uint32_t irq = ch32rv_irq_save();
            port._errors |= (uint8_t)marks;
            ch32rv_irq_restore(irq);
        }
        const uint32_t t = r.tail;
        return pos >= t ? pos - t : pos + n - t;
    }

    /* Always the DMA's count now: onReceive() takes its FIFO-full count and
     * its callback's available() from here (counting only what an earlier
     * look saw broke both, bench 2026-11-17). The full look (the flags twice,
     * the USART's errors) only once the DMA passed its half or end point - a
     * lap always passes one - and in read() / peek() past the last look's
     * bytes: a full look per available() kept CH32V003 at 48 MHz below
     * 1 Mbaud reading byte by byte (UART_BUFFER_FULL_ERROR, 3756 of 4096,
     * bench 2026-11-21). */
    static int available(CH32RVHardwareSerial &port)
    {
        Rx &r = of(port);
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t count = CH32RV_DMA_CNTR(l);
        if ((CH32RV_DMA_INTFR(l) >> l->shift) & (CH32RV_DMA_HTIF | CH32RV_DMA_TCIF)) {
            return (int)look(port);
        }
        __asm__ volatile("fence rw, rw" ::: "memory");
        const uint32_t n = r.size, pos = place(n, count), t = r.tail;
        return (int)(pos >= t ? pos - t : pos + n - t);
    }

    static int peek(CH32RVHardwareSerial &port)
    {
        Rx &r = of(port);
        /* Bytes the last look saw are read without another. */
        if (r.tail == r.last && !look(port)) {
            return -1;
        }
        return r.buf[r.tail] & port._rx_mask;
    }

    static int read(CH32RVHardwareSerial &port)
    {
        Rx &r = of(port);
        if (r.tail == r.last && !look(port)) {
            return -1;
        }
        const uint32_t t = r.tail;
        r.tail = (uint16_t)ch32rv_serial_dma_wrap(t + 1u, r.size);
        return r.buf[t] & port._rx_mask;
    }

    /* End of begin(): the channel claimed and running. RTS flow control
     * follows the data register, which the DMA would empty at once: such a
     * port receives as without. */
    static void start(CH32RVHardwareSerial &port)
    {
        Rx &r = of(port);
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t base = port.registers();
        port._dma &= (uint8_t)~CH32RV_SERIAL_DMA_RX;
        if (!r.size || !l->channel || (CH32RV_USART_CTLR3(base) & CH32RV_USART_CTLR3_RTSE) ||
            !ch32rv_dma_claim(l->dma, l->channel)) {
            return;
        }
        ch32rv_dma_open(l);
        CH32RV_DMA_PADDR(l) = base + 0x04u;    /* DATAR */
        CH32RV_DMA_MADDR(l) = (uint32_t)(uintptr_t)r.buf;
        CH32RV_DMA_CNTR(l) = r.size;
        CH32RV_DMA_CFGR(l) = CH32RV_DMA_CFGR_MINC | CH32RV_DMA_CFGR_CIRC | CH32RV_DMA_CFGR_PL(3) |
                             CH32RV_DMA_CFGR_EN;
        r.tail = r.last = 0;
        r.seen = 0;
        const uint32_t irq = ch32rv_irq_save();
        port._dma |= CH32RV_SERIAL_DMA_RX;
        CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_RXNEIE;
        CH32RV_USART_CTLR3(base) |= CH32RV_USART_CTLR3_DMAR;
        ch32rv_irq_restore(irq);
    }

    static void end(CH32RVHardwareSerial &port)
    {
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t base = port.registers();
        const uint32_t irq = ch32rv_irq_save();
        port._dma &= (uint8_t)~CH32RV_SERIAL_DMA_RX;
        CH32RV_USART_CTLR3(base) &= (uint16_t)~CH32RV_USART_CTLR3_DMAR;
        ch32rv_irq_restore(irq);
        ch32rv_dma_close(l);
        ch32rv_dma_release(l->dma, l->channel);
    }

    static size_t set(CH32RVHardwareSerial &port, size_t size, uint8_t *storage, bool own);
};

}  // namespace arduino

namespace {

const CH32RVSerialDmaRxOps rx_ops = {
    CH32RVSerialDmaRx::start, CH32RVSerialDmaRx::end, CH32RVSerialDmaRx::available,
    CH32RVSerialDmaRx::peek, CH32RVSerialDmaRx::read, CH32RVSerialDmaRx::position,
};

}  // namespace

size_t CH32RVSerialDmaRx::set(CH32RVHardwareSerial &port, size_t size, uint8_t *storage, bool own)
{
    Rx &r = of(port);
    if (r.own && heap_free && r.buf != storage) {
        heap_free(r.buf);
    }
    r.buf = storage;
    r.size = (uint16_t)size;
    r.own = own;
    ch32rv_serial_dma_rx = &rx_ops;
    return size;
}

size_t CH32RVHardwareSerial::rxBufferBuiltin(size_t size)
{
    size = ch32rv_serial_dma_size(_slot != 0, size);
    if (!size || size > CH32RV_SERIAL_RX_BUFFER_SIZE) {
        return 0;
    }
    return CH32RVSerialDmaRx::set(*this, size, _rx.data(), false);
}

size_t CH32RVHardwareSerial::rxBufferHeap(size_t size)
{
    if (size <= CH32RV_SERIAL_RX_BUFFER_SIZE) {
        return rxBufferBuiltin(size);
    }
    size = ch32rv_serial_dma_size(_slot != 0, size);
    if (!size) {
        return 0;
    }
    /* The heap block of an earlier call is used again where it is large
     * enough, and otherwise given back before the new one is taken: two at
     * once did not fit CH32V003's 2 KB (bench 2026-11-17, a second begin()'s
     * sizes refused). A new block that does not fit leaves the direction
     * off DMA (size 0). */
    Rx &d = CH32RVSerialDmaRx::of(*this);
    uint8_t *buf = d.own && d.cap >= size ? d.buf : nullptr;
    if (!buf) {
        if (d.own) {
            free(d.buf);
            d.buf = nullptr;
            d.size = 0;
            d.own = 0;
        }
        buf = (uint8_t *)malloc(size);
        if (!buf) {
            return 0;
        }
        d.cap = (uint16_t)size;
    }
    heap_free = free;
    return CH32RVSerialDmaRx::set(*this, size, buf, true);
}

#endif
