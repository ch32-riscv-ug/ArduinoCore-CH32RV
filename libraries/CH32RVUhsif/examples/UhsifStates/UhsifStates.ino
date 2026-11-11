// en: Program the UHSIF state machine: sample the data lines every clock into a ring and count the buffers.
// ja: UHSIFの状態機械を書き、データ線を毎クロック取り込んでリングに書き、バッファの数を数える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32H417 */

#include <CH32RVUhsif.h>

CH32RVUhsif uhsif;

// en: Four buffers of 4096 bytes (2048 samples) that thread 0 writes in turn.
// ja: スレッド0が順に書く、4096バイト(2048サンプル)のバッファ4つ。
static const uint16_t BUFFER_BYTES = 4096;
static const uint8_t BUFFERS = 4;
static uint16_t ring[BUFFERS * BUFFER_BYTES / 2] __attribute__((aligned(32)));
static volatile uint32_t filledBuffers;

void setup()
{
    Serial.begin(115200);
    // en: SYSCLK / 64 = 6.25 MHz on a 400 MHz SYSCLK.
    // ja: SYSCLK / 64。SYSCLKが400 MHzなら6.25 MHz。
    if (!uhsif.begin(63)) {
        Serial.printf("begin: %s\r\n", uhsif.lastErrorName());
        return;
    }
    using In = CH32RVUhsifInput;
    const uint8_t S = CH32RVUhsif::SampleData;
    const uint32_t P = CH32RVUhsif::Push, C = CH32RVUhsif::CountData;
    const uint32_t R = CH32RVUhsif::ResetDataCount | CH32RVUhsif::Interrupt;
    // en: Every transition latches the lines and pushes the sample, so the machine
    // en: alternates two states (a transition into the same state would not latch again).
    // en: State 1 leaves by `right` when the data counter hits its limit: that
    // en: transition resets the counter and raises the interrupt, once per buffer.
    // ja: どの遷移も端子をラッチして積むので、2つの状態を行き来する(同じ状態へ戻る遷移はラッチし直さない)。
    // ja: 状態1はデータカウンタが上限に来たら右へ出る。その遷移でカウンタを戻し、割り込みを出す(バッファに1回)。
    static const CH32RVUhsifState states[] = {
        // inputs a..d                                    goRight   left                right
        {{In::DataCountHit, In::One, In::One, In::One}, 0x0000, {1, S, P | C}, {1, S, P | C}},
        {{In::DataCountHit, In::One, In::One, In::One}, 0xAAAA, {0, S, P | C}, {0, S, P | R}},
    };
    const CH32RVUhsifTransition start = {0, S, P | CH32RVUhsif::ResetDataCount};
    // en: The counter's limit is the samples a buffer holds, minus 2.
    // ja: カウンタの上限は、1つのバッファのサンプル数から2を引いた値。
    if (!uhsif.setStates(states, 2, start) || !uhsif.setDataCounter(BUFFER_BYTES / 2 - 2) ||
        !uhsif.setRing(0, ring, BUFFER_BYTES, BUFFERS, BUFFER_BYTES)) {
        Serial.printf("setup: %s\r\n", uhsif.lastErrorName());
        return;
    }
    uhsif.onInterrupt([] { filledBuffers++; });
    uhsif.start();
    Serial.printf("UHSIF at %lu Hz\r\n", (unsigned long)uhsif.clockHz());
}

void loop()
{
    static uint32_t last, lastInterrupts, lastBytes;
    if (!uhsif.running() || millis() - last < 1000) {
        return;
    }
    last = millis();
    const uint32_t n = filledBuffers, bytes = uhsif.bytesWritten(0);
    // en: The newest buffer: the one before the buffer being written.
    // ja: いちばん新しく埋まったバッファ: いま書いているものの1つ前。
    const uint16_t *newest = ring + ((n + BUFFERS - 1) % BUFFERS) * (BUFFER_BYTES / 2);
    Serial.printf("%lu buffers/s, %lu bytes/s, DATA0-15 = %04x\r\n", (unsigned long)(n - lastInterrupts),
                  (unsigned long)(bytes - lastBytes), newest[0]);
    lastInterrupts = n;
    lastBytes = bytes;
}
