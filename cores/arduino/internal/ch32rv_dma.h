/* One DMA channel as a peripheral's request reaches it, and who holds it.
 *
 * Every family here has the same channel: CFGR, CNTR, PADDR, MADDR at 0x0,
 * 0x4, 0x8, 0xC of its block (device-data register_layouts.csv), and four flag
 * bits (GIF, TCIF, HTIF, TEIF) in an INTFR whose INTFCR is the next word. What
 * moves between families is where a channel's block and flags are - channels
 * 8 and up on V30x / V4x7 and 9 and up on X3x5 keep their flags in another
 * register - and, on CH32H41x, that no request is fixed to a channel: DMAMUX
 * hands a channel its request. The variant names each line it uses already
 * resolved (tools/generate/generate.py, load_uart_dma):
 *
 *   CH32RV_DMA_LINE(dma, channel, channel registers, INTFR, flag shift, request)
 *
 * request 0 where the request is the channel's fixed one, the DMAMUX request
 * number on H41x. ch32rv_dma_claim() keeps two users off one channel: the
 * first to claim it has it until it lets go (HardwareSerial's DMA buffers,
 * CH32RVAdcStream, CH32RVLogicCapture). ch32rv_dma.c, linked when one of them
 * is. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pins_arduino.h"
#include "internal/ch32rv_registers.h"

typedef struct {
    uint32_t ch;        /* the channel's CFGR address              */
    uint32_t flags;     /* its INTFR (INTFCR is the next word)     */
    uint8_t shift;      /* its four flags from this bit            */
    uint8_t dma;        /* 1, 2                                    */
    uint8_t channel;    /* 1..                                     */
    uint8_t request;    /* DMAMUX request number; 0: fixed         */
} ch32rv_dma_line_t;

#define CH32RV_DMA_LINE(dma, channel, ch, flags, shift, request) \
    { (ch), (flags), (uint8_t)(shift), (uint8_t)(dma), (uint8_t)(channel), (uint8_t)(request) }

#define CH32RV_DMA_CFGR(l)  CH32RV_REG32((l)->ch + 0x0u)
#define CH32RV_DMA_CNTR(l)  CH32RV_REG32((l)->ch + 0x4u)
#define CH32RV_DMA_PADDR(l) CH32RV_REG32((l)->ch + 0x8u)
#define CH32RV_DMA_MADDR(l) CH32RV_REG32((l)->ch + 0xCu)
#define CH32RV_DMA_INTFR(l)  CH32RV_REG32((l)->flags)
#define CH32RV_DMA_INTFCR(l) CH32RV_REG32((l)->flags + 0x4u)

/* CFGR */
#define CH32RV_DMA_CFGR_EN   (1u << 0)
#define CH32RV_DMA_CFGR_TCIE (1u << 1)
#define CH32RV_DMA_CFGR_HTIE (1u << 2)
#define CH32RV_DMA_CFGR_DIR  (1u << 4)    /* memory to peripheral */
#define CH32RV_DMA_CFGR_CIRC (1u << 5)
#define CH32RV_DMA_CFGR_MINC (1u << 7)
#define CH32RV_DMA_CFGR_PL(n) ((uint32_t)(n) << 12)   /* 0 low .. 3 very high */

/* A channel's flags, shifted down to bits 0..3. */
#define CH32RV_DMA_GIF  (1u << 0)
#define CH32RV_DMA_TCIF (1u << 1)
#define CH32RV_DMA_HTIF (1u << 2)
#define CH32RV_DMA_TEIF (1u << 3)

#ifdef __cplusplus
extern "C" {
#endif

/* The channel for the caller alone: false when another holds it. */
bool ch32rv_dma_claim(uint8_t dma, uint8_t channel);
void ch32rv_dma_release(uint8_t dma, uint8_t channel);

/* The same two for a user whose channel only a later user can want (the
 * ADC's runs: their channel is fixed and theirs first): through this
 * pointer, null until a file that claims by name runs its static
 * initialisation (ch32rv_dma_claims_link(): HardwareSerial's DMA buffers -
 * CH32RVLogicCapture claims by name too, but never the ADC's channel).
 * Without one in the sketch nothing ever writes it, the
 * link-time optimiser takes it for null, and those users' claims fold away. */
typedef struct {
    bool (*claim)(uint8_t dma, uint8_t channel);
    void (*release)(uint8_t dma, uint8_t channel);
} ch32rv_dma_claims_t;
extern const ch32rv_dma_claims_t *ch32rv_dma_claims;
void ch32rv_dma_claims_link(void);

#ifdef __cplusplus
}
#endif

/* The line's DMA clocked and, on H41x, its channel given the line's request
 * (DMAMUX: one byte per channel, DMA1's eight then DMA2's; the byte holds the
 * request number minus one - EVT ch32h417_dma.c DMA_MuxChannelConfig). The
 * channel off and its flags cleared. */
static inline void ch32rv_dma_open(const ch32rv_dma_line_t *l)
{
#ifdef CH32RV_CLKEN_DMA2_ADDR
    if (l->dma == 2) {
        ch32rv_clock_enable(DMA2);
    } else
#endif
    {
        ch32rv_clock_enable(DMA1);
    }
#ifdef CH32RV_DMAMUX_BASE
    if (l->request) {
        const uint32_t k = (uint32_t)(l->dma - 1u) * 8u + (uint32_t)(l->channel - 1u);
        const uint32_t addr = CH32RV_DMAMUX_BASE + (k / 4u) * 4u;
        const uint32_t at = (k % 4u) * 8u;
        CH32RV_REG32(addr) = (CH32RV_REG32(addr) & ~(0x7Fu << at)) | ((uint32_t)(l->request - 1u) << at);
    }
#endif
    CH32RV_DMA_CFGR(l) = 0;
    CH32RV_DMA_INTFCR(l) = 0xFu << l->shift;
}

/* Off, its flags cleared. */
static inline void ch32rv_dma_close(const ch32rv_dma_line_t *l)
{
    CH32RV_DMA_CFGR(l) = 0;
    CH32RV_DMA_INTFCR(l) = 0xFu << l->shift;
}
