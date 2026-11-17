/* setTxBufferSize() (HardwareSerial.h): the port's transmit buffer sent by
 * DMA. HardwareSerialDmaRx.cpp is the receive side; like it, reached only
 * through a pointer this file alone writes (ch32rv_serial_dma_tx), so a sketch
 * that does not call setTxBufferSize() links none of it.
 *
 * write() puts the byte in the buffer, and one channel in normal mode sends
 * what lies there in one run, up to the buffer's end. The USART's
 * transmit-complete interrupt, after the run's last stop bit, starts the next
 * - a gap of the interrupt's entry between runs; write() and flush() start it
 * themselves as soon as the run's count is 0 when they wait for room, so a
 * sketch that writes faster than the line sends without gaps (and interrupts
 * off do not stop it). No DMA interrupt: its vectors stay the libraries'. */
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

const ch32rv_dma_line_t lines[] = CH32RV_SERIAL_DMA_LINES(tx);

struct Tx {
    uint8_t *buf;
    uint16_t size;          /* 0: setTxBufferSize() not called           */
    uint8_t own;            /* buf is from the heap                      */
    volatile uint16_t head; /* write()'s place                           */
    volatile uint16_t tail; /* the first byte not yet sent               */
    volatile uint16_t run;  /* bytes in the run in flight; 0: none       */
};
Tx ports[CH32RV_SERIAL_COUNT];

void (*heap_free)(void *);   /* as in HardwareSerialDmaRx.cpp */

}  // namespace

namespace arduino {

struct CH32RVSerialDmaTx {
    static Tx &of(CH32RVHardwareSerial &port) { return ports[CH32RV_SERIAL_COUNT == 1 ? 0 : port._number]; }
    static const ch32rv_dma_line_t *line(CH32RVHardwareSerial &port) { return &lines[CH32RV_SERIAL_COUNT == 1 ? 0 : port._slot - 1u]; }

    /* The next run: from the first byte not sent to write()'s place or the
     * buffer's end. Interrupts off, or in the port's interrupt. */
    static void kick(Tx &t, const ch32rv_dma_line_t *l, uint32_t base)
    {
        const uint32_t head = t.head, tail = t.tail;
        if (t.run || head == tail) {
            return;
        }
        const uint32_t len = head > tail ? head - tail : t.size - tail;
        CH32RV_DMA_CFGR(l) &= ~CH32RV_DMA_CFGR_EN;
        CH32RV_DMA_MADDR(l) = (uint32_t)(uintptr_t)(t.buf + tail);
        CH32RV_DMA_CNTR(l) = len;
        /* TC cleared (written 0) before the run, so that it rises at this
         * run's end and not for the last. */
        CH32RV_USART_STATR(base) = (uint16_t)~CH32RV_USART_STATR_TC;
        CH32RV_DMA_CFGR(l) |= CH32RV_DMA_CFGR_EN;
        t.run = (uint16_t)len;
        CH32RV_USART_CTLR1(base) |= CH32RV_USART_CTLR1_TCIE;
    }

    /* The run in flight, once the DMA has handed its last byte on: its room
     * free, the next run started. */
    static void advance(Tx &t, const ch32rv_dma_line_t *l, uint32_t base)
    {
        const uint32_t run = t.run;
        if (run && CH32RV_DMA_CNTR(l) == 0u) {
            t.tail = (uint16_t)ch32rv_serial_dma_wrap(t.tail + run, t.size);
            t.run = 0;
            kick(t, l, base);
        }
    }

    static void poll(CH32RVHardwareSerial &port)
    {
        const uint32_t irq = ch32rv_irq_save();
        advance(of(port), line(port), port.registers());
        ch32rv_irq_restore(irq);
    }

    static void write(CH32RVHardwareSerial &port, uint8_t c)
    {
        Tx &t = of(port);
        const uint32_t head = t.head;
        const uint32_t next = ch32rv_serial_dma_wrap(head + 1u, t.size);
        /* Full: wait for the run in flight, moving on here when it is done
         * (interrupts may be off). */
        while (next == t.tail) {
            poll(port);
        }
        t.buf[head] = c;
        t.head = (uint16_t)next;
        const uint32_t irq = ch32rv_irq_save();
        kick(t, line(port), port.registers());
        ch32rv_irq_restore(irq);
    }

    static void flush(CH32RVHardwareSerial &port)
    {
        Tx &t = of(port);
        while (t.run || t.head != t.tail) {
            poll(port);
        }
    }

    static int room(CH32RVHardwareSerial &port)
    {
        const Tx &t = of(port);
        const uint32_t head = t.head, tail = t.tail;
        return (int)((head >= tail ? tail + t.size : tail) - head - 1u);
    }

    /* Bytes not yet handed to the USART: the RS485 driver enable stays on. */
    static bool sending(CH32RVHardwareSerial &port)
    {
        const Tx &t = of(port);
        const uint32_t run = t.run;
        if (!run) {
            return t.head != t.tail;
        }
        return CH32RV_DMA_CNTR(line(port)) != 0u || ch32rv_serial_dma_wrap(t.tail + run, t.size) != t.head;
    }

    /* After the port's interrupt: a run ended (TC with TCIE), the next. */
    static void irq(CH32RVHardwareSerial &port, uint32_t base)
    {
        if (!(CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TC)) {
            return;
        }
        Tx &t = of(port);
        advance(t, line(port), base);
        if (!t.run) {
            CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TCIE;
        }
    }

    static void start(CH32RVHardwareSerial &port)
    {
        Tx &t = of(port);
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t base = port.registers();
        port._dma &= (uint8_t)~CH32RV_SERIAL_DMA_TX;
        if (!t.size || !l->channel || !ch32rv_dma_claim(l->dma, l->channel)) {
            return;
        }
        ch32rv_dma_open(l);
        CH32RV_DMA_PADDR(l) = base + 0x04u;    /* DATAR */
        CH32RV_DMA_CFGR(l) = CH32RV_DMA_CFGR_DIR | CH32RV_DMA_CFGR_MINC | CH32RV_DMA_CFGR_PL(2);
        t.head = t.tail = t.run = 0;
        const uint32_t irq = ch32rv_irq_save();
        port._dma |= CH32RV_SERIAL_DMA_TX;
        CH32RV_USART_CTLR3(base) |= CH32RV_USART_CTLR3_DMAT;
        ch32rv_irq_restore(irq);
    }

    static void end(CH32RVHardwareSerial &port)
    {
        const ch32rv_dma_line_t *l = line(port);
        const uint32_t base = port.registers();
        const uint32_t irq = ch32rv_irq_save();
        port._dma &= (uint8_t)~CH32RV_SERIAL_DMA_TX;
        CH32RV_USART_CTLR3(base) &= (uint16_t)~CH32RV_USART_CTLR3_DMAT;
        CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TCIE;
        ch32rv_irq_restore(irq);
        ch32rv_dma_close(l);
        ch32rv_dma_release(l->dma, l->channel);
        Tx &t = of(port);
        t.head = t.tail = t.run = 0;
    }

    static size_t set(CH32RVHardwareSerial &port, size_t size, uint8_t *storage, bool own);
};

}  // namespace arduino

namespace {

const CH32RVSerialDmaTxOps tx_ops = {
    CH32RVSerialDmaTx::start, CH32RVSerialDmaTx::end, CH32RVSerialDmaTx::irq,
    CH32RVSerialDmaTx::write, CH32RVSerialDmaTx::flush, CH32RVSerialDmaTx::room,
    CH32RVSerialDmaTx::sending,
};

}  // namespace

size_t CH32RVSerialDmaTx::set(CH32RVHardwareSerial &port, size_t size, uint8_t *storage, bool own)
{
    Tx &t = of(port);
    if (t.own && heap_free && t.buf != storage) {
        heap_free(t.buf);
    }
    t.buf = storage;
    t.size = (uint16_t)size;
    t.own = own;
    ch32rv_serial_dma_tx = &tx_ops;
    return size;
}

size_t CH32RVHardwareSerial::txBufferBuiltin(size_t size)
{
    size = ch32rv_serial_dma_size(_slot != 0, size);
    if (!size || size > CH32RV_SERIAL_TX_BUFFER_SIZE) {
        return 0;
    }
    return CH32RVSerialDmaTx::set(*this, size, _tx.data(), false);
}

size_t CH32RVHardwareSerial::txBufferHeap(size_t size)
{
    if (size <= CH32RV_SERIAL_TX_BUFFER_SIZE) {
        return txBufferBuiltin(size);
    }
    size = ch32rv_serial_dma_size(_slot != 0, size);
    uint8_t *buf = size ? (uint8_t *)malloc(size) : nullptr;
    if (!buf) {
        return 0;
    }
    heap_free = free;
    return CH32RVSerialDmaTx::set(*this, size, buf, true);
}

#endif
