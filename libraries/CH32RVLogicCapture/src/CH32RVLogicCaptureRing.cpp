/* CH32RVLogicCapture's deeper ring (startRing() / readBlock()): its own file and its own state, so a sketch that
 * does not use it pays nothing - not even in the object (CH32V003 has 2 KB of RAM: CH32RVLogicCapture.h). One
 * capture runs at a time (one timer and DMA channel), so one ring is enough. */
#include "CH32RVLogicCapture.h"

#if CH32RV_LOGIC_CAPTURE

static uint16_t *ringBuffer;
static uint32_t ringBlock;       /* samples in a block */
static uint8_t ringBlocks;
static uint32_t ringRead;        /* blocks handed over since the start */
static uint32_t ringSkipped;

bool CH32RVLogicCapture::startRing(uint16_t *buffer, uint32_t samples, uint8_t blocks)
{
    if (_backend || blocks < 2u || blocks > 16u || samples == 0u || samples > 65534u ||
        samples % (2u * blocks) != 0u) {
        return false;
    }
    if (!startContinuous(buffer, samples)) {
        return false;
    }
    ringBuffer = buffer;
    ringBlocks = blocks;
    ringBlock = samples / blocks;
    ringRead = 0;
    ringSkipped = 0;
    return true;
}

const uint16_t *CH32RVLogicCapture::readBlock()
{
    if (!ringBuffer || !_running) {
        return nullptr;
    }
    const uint32_t written = samplesWritten();
    const uint32_t full = written / ringBlock;          /* blocks written whole */
    /* The block being written is gone for the reader, and so is everything a whole ring behind it. */
    if (full > ringRead + (uint32_t)(ringBlocks - 1u)) {
        const uint32_t oldest = full - (uint32_t)(ringBlocks - 1u);
        ringSkipped += oldest - ringRead;
        ringRead = oldest;
    }
    if (full <= ringRead) {
        return nullptr;
    }
    const uint16_t *block = ringBuffer + (ringRead % ringBlocks) * ringBlock;
    ringRead++;
    __asm__ volatile("fence rw, rw" ::: "memory");      /* the DMA wrote it: no stale copy */
    return block;
}

uint32_t CH32RVLogicCapture::blockSamples() const
{
    return ringBlock;
}

uint32_t CH32RVLogicCapture::blockOverruns() const
{
    return ringSkipped;
}

#endif
