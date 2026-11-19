/* DmaCopy: a copy by DMA while the CPU keeps counting, then a fill, each checked.
 * Prints the channel, how many turns the CPU made while the copy ran, and whether the data is right. */
#include <CH32RVDma.h>

CH32RVDma dma;
alignas(4) static uint8_t source[256], target[256];   // small enough for the 2 KiB parts
static volatile bool finished;

static void done(void *)
{
    finished = true;                // in the channel's interrupt
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    for (size_t i = 0; i < sizeof source; i++) {
        source[i] = (uint8_t)(i * 7 + 3);
    }
    if (!dma.begin()) {
        Serial.println("no free DMA channel");
        return;
    }
    Serial.print("DMA");
    Serial.print(dma.dma());
    Serial.print(" channel ");
    Serial.println(dma.channel());

    dma.onDone(done);
    uint32_t turns = 0;
    dma.copyAsync(target, source, sizeof source);
    while (!finished) {
        turns++;                    // the CPU is free while the channel copies
    }
    Serial.print("copy: ");
    Serial.print(memcmp(target, source, sizeof source) == 0 ? "same" : "DIFFERENT");
    Serial.print(", the CPU counted to ");
    Serial.println(turns);

    dma.fill(target, 0xA5, sizeof target);
    bool filled = true;
    for (size_t i = 0; i < sizeof target; i++) {
        filled = filled && target[i] == 0xA5;
    }
    Serial.print("fill: ");
    Serial.println(filled ? "all 0xA5" : "WRONG");
}

void loop()
{
}
