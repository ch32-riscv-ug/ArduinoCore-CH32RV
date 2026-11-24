/* Memory-to-memory transfers by a DMA channel: a copy or a fill, waiting for it or running in the background while
 * the CPU does other work, with a callback when it ends.
 *
 *     CH32RVDma dma;
 *     dma.begin();                        // a free channel of its own
 *     dma.copy(dst, src, n);              // waits
 *     dma.onDone(done);                   // done() in the channel's interrupt after each background job
 *     dma.copyAsync(dst, src, n);         // returns at once; busy() / wait()
 *     dma.fill(dst, 0, n);                // n bytes of one value
 *
 * begin() takes a channel nothing holds (ch32rv_dma_claim()) whose interrupt nothing links a handler for
 * (ch32rv_irq_attach()), from the last channel down: the first channels are the ones the peripherals' fixed requests
 * (Serial's DMA buffers, CH32RVAdcStream, SD_MMC) are on. Each transfer moves words when both addresses and the
 * length are multiples of 4, halfwords for multiples of 2, else bytes; a long one goes in parts of 65535 units, the
 * next started from the interrupt. The CPU's own memcpy() is faster on the fast cores (a 4 KiB copy: X035 218 us by
 * memcpy against 140 us by DMA, V203 24 against 44, H417 28 against 53 - 2026-11-19): a DMA copy pays when the CPU
 * has other work meanwhile. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>
#include <CH32RVCallback.h>

class CH32RVDma {
public:
    /* A function, or a lambda capturing at most two pointers (CH32RVCallback.h). */
    typedef CH32RVCallback<void()> Callback;

    bool begin();
    void end();
    bool isAttached() const { return _index >= 0; }
    /* DMA block and channel begin() took (0 before). */
    uint8_t dma() const;
    uint8_t channel() const;

    /* false (nothing moved) before begin(), while a job runs, or for 0 bytes. */
    bool copy(void *dst, const void *src, size_t bytes);
    bool copyAsync(void *dst, const void *src, size_t bytes);
    bool fill(void *dst, uint8_t value, size_t bytes);
    bool fillAsync(void *dst, uint8_t value, size_t bytes);

    /* Called in the channel's interrupt when a background job (copyAsync() / fillAsync()) ends; nullptr for
     * none. */
    void onDone(Callback cb);
    bool busy() const { return _left != 0u || _running; }
    /* Until the job ends (it is driven from here too when interrupts are masked). */
    void wait();
    /* Stops a job; what it moved stays. */
    void abort();

    /* Run from the channel's interrupt (internal). */
    void service();

private:
    bool start(void *dst, const void *src, size_t bytes, bool fill, uint8_t value);
    bool run(void *dst, const void *src, size_t bytes, bool fill, uint8_t value);
    void next();

    int8_t _index = -1;             /* into the variant's CH32RV_DMA_CHANNELS */
    volatile bool _running = false; /* a part is on the channel */
    volatile size_t _left = 0;      /* units after the running part */
    uint32_t _dst = 0, _src = 0;    /* the next part's addresses */
    uint8_t _unit = 1;              /* bytes a unit */
    bool _fill = false;
    uint32_t _pattern = 0;          /* the fill's value, in every byte */
    Callback _cb;
};
