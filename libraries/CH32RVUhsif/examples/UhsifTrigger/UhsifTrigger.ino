// en: Wait for a rising edge on DATA2 with the data comparator, then take 4096 samples of DATA0-7 (one byte each) once.
// ja: データの比較器でDATA2の立ち上がりを待ち、そこからDATA0〜7を4096サンプル(1サンプル1バイト)1回だけ取り込む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* board: CH32H417 */

#include <CH32RVUhsif.h>

CH32RVUhsif uhsif;

static const uint16_t SAMPLES = 4096;
// en: 64 bytes more: the DMA writes 64-byte bursts, so the last part of a burst stays in the
// en: block when the machine stops pushing; pushing 64 more bytes moves all 4096 out.
// ja: 64バイト多く取る: DMAは64バイトのburstで書くので、積むのをやめるとburstの端数がブロックに
// ja: 残る。64バイト余分に積むと、4096サンプルはすべて書き出される。
static const uint16_t PAD = 64;
static uint8_t capture[SAMPLES + PAD] __attribute__((aligned(32)));
static const uint8_t TRIGGER_BIT = 2;   // DATA2 = PD12

// en: The machine: 0/1 wait while DATA2 is high, 2/3 wait while it is low, then
// en: 4/5 latch and push every clock, counting, and 6/7 idle once the counter hits.
// en: Two states for each step because a transition into the state it leaves does
// en: not decide again (README of the library).
// ja: 状態0/1はDATA2がHighの間待ち、2/3はLowの間待つ。4/5で毎クロックラッチして積み、数え、
// ja: カウンタが上限に来たら6/7で止まる。どの段も2つの状態にする(同じ状態へ戻る遷移は判断し直さない)。
static bool startCapture()
{
    using In = CH32RVUhsifInput;
    const uint8_t S = CH32RVUhsif::SampleData;
    const uint32_t PC = CH32RVUhsif::Push | CH32RVUhsif::CountData;
    const uint16_t MATCH = 0xAAAA, NO_MATCH = 0x5555;   // goRight on DataMatch true / false (input a)
    static const CH32RVUhsifState states[] = {
        {{In::DataMatch, In::One, In::One, In::One}, NO_MATCH, {1, 0, 0}, {2, 0, 0}},
        {{In::DataMatch, In::One, In::One, In::One}, NO_MATCH, {0, 0, 0}, {2, 0, 0}},
        {{In::DataMatch, In::One, In::One, In::One}, MATCH, {3, 0, 0}, {4, S, PC}},
        {{In::DataMatch, In::One, In::One, In::One}, MATCH, {2, 0, 0}, {4, S, PC}},
        {{In::DataCountHit, In::One, In::One, In::One}, MATCH, {5, S, PC}, {6, 0, 0}},
        {{In::DataCountHit, In::One, In::One, In::One}, MATCH, {4, S, PC}, {6, 0, 0}},
        {{In::One, In::One, In::One, In::One}, 0, {7, 0, 0}, {7, 0, 0}},
        {{In::One, In::One, In::One, In::One}, 0, {6, 0, 0}, {6, 0, 0}},
    };
    const CH32RVUhsifTransition start = {0, 0, CH32RVUhsif::ResetDataCount};
    memset(capture, 0x5A, sizeof capture);
    // en: SYSCLK / 2 = 200 MS/s on a 400 MHz SYSCLK.
    // ja: SYSCLK / 2。SYSCLKが400 MHzなら200 MS/s。
    return uhsif.begin(1) && uhsif.setBusWidth(8) && uhsif.setDataMatch(1u << TRIGGER_BIT, 1u << TRIGGER_BIT) &&
           uhsif.setStates(states, 8, start) && uhsif.setDataCounter(SAMPLES + PAD - 2) &&
           uhsif.setRing(0, capture, SAMPLES + PAD, 1, SAMPLES + PAD) && uhsif.start();
}

void setup()
{
    Serial.begin(115200);
    // en: The signal: TIM PWM on PD12 (DATA2), duty 1/4.
    // ja: 信号: PD12(DATA2)にタイマーのPWM、duty 1/4。
    analogWriteFrequency(PD12, 100000);
    analogWrite(PD12, 64);
}

void loop()
{
    if (!startCapture()) {
        Serial.printf("UHSIF: %s\r\n", uhsif.lastErrorName());
        delay(1000);
        return;
    }
    delay(10);                    // en: 4096 samples take 20 us / ja: 4096サンプルは20 us
    const uint32_t hz = uhsif.clockHz();
    uhsif.stop();
    // en: Samples from the rising edge: the first falls and rises give the high time and the period.
    // ja: 立ち上がりからのサンプル: 最初の立ち下がりと次の立ち上がりで、High の長さと周期が出る。
    int fall = -1, rise = -1, unwritten = 0;
    for (int i = 1; i < SAMPLES; i++) {
        if (capture[i] == 0x5A) {
            unwritten++;
            continue;
        }
        const bool now = capture[i] >> TRIGGER_BIT & 1, before = capture[i - 1] >> TRIGGER_BIT & 1;
        if (fall < 0 && before && !now) {
            fall = i;
        } else if (fall >= 0 && rise < 0 && !before && now) {
            rise = i;
        }
    }
    Serial.printf("first sample %02x, high %d samples, period %d samples (%.1f kHz at %lu Hz), unwritten %d\r\n",
                  capture[0], fall, rise, rise > 0 ? hz / 1000.0 / rise : 0.0, (unsigned long)hz, unwritten);
    delay(1000);
}
