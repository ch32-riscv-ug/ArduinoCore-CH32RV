/* CH32RV_DMA_MEM: a buffer a peripheral's DMA moves, kept in the shared SRAM.
 *
 * On CH32H41x with the sketch on the V5F (CPU Core = V5F), .data, .bss and the
 * stack are in the V5F's DTCM (v5f/sections_loadcode.ld); the heap
 * (malloc / new) and what is marked CH32RV_DMA_MEM are in the shared SRAM.
 * Every DMA reaches both (measured 2026-11-22: DMA1 copies, ECDC, GPHA, the
 * HSADC and the USBSS IN stream), but a DMA write into the TCMs is slower - a
 * timer-paced DMA1 capture lost about half its samples there at 12.5 MS/s,
 * none in the shared SRAM (docs/h417-capture.ja.md) - and it takes the DTCM's
 * port from the V5F's own loads and stores. Mark buffers that stream at
 * high rates:
 *
 *   static uint16_t samples[16384] CH32RV_DMA_MEM;   // zero-initialised only
 *
 * Everywhere else every RAM is the same RAM and the mark changes nothing.
 */
#pragma once

#define CH32RV_DMA_MEM_STR2(x) #x
#define CH32RV_DMA_MEM_STR(x) CH32RV_DMA_MEM_STR2(x)
/* One input section per object (__COUNTER__), so --gc-sections drops each
 * unused buffer on its own; ".bss." makes it NOBITS and lets every other
 * layout's *(.bss*) take it. Word-aligned, as ESP-IDF's DMA_ATTR; add
 * __attribute__((aligned(16))) where a controller wants more. */
#define CH32RV_DMA_MEM \
    __attribute__((section(".bss.ch32rv_dma." CH32RV_DMA_MEM_STR(__COUNTER__)), aligned(4)))
