/* Who holds which DMA channel (internal/ch32rv_dma.h). One bit per channel,
 * DMA1's from bit 0 and DMA2's from bit 16 (no DMA here has more than 16).
 * Linked only by what claims a channel. */
#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_irq.h"

static uint32_t claimed;

static inline uint32_t bit_of(uint8_t dma, uint8_t channel)
{
    return 1u << ((uint32_t)(dma - 1u) * 16u + (uint32_t)(channel - 1u));
}

bool ch32rv_dma_claim(uint8_t dma, uint8_t channel)
{
    const uint32_t bit = bit_of(dma, channel);
    const uint32_t irq = ch32rv_irq_save();
    const bool free = (claimed & bit) == 0u;
    claimed |= bit;
    ch32rv_irq_restore(irq);
    return free;
}

void ch32rv_dma_release(uint8_t dma, uint8_t channel)
{
    const uint32_t irq = ch32rv_irq_save();
    claimed &= ~bit_of(dma, channel);
    ch32rv_irq_restore(irq);
}

static const ch32rv_dma_claims_t claims = {ch32rv_dma_claim, ch32rv_dma_release};
const ch32rv_dma_claims_t *ch32rv_dma_claims = 0;

void ch32rv_dma_claims_link(void)
{
    ch32rv_dma_claims = &claims;
}
