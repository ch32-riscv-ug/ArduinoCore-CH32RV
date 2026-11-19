// en: One more serial receiver: the PIOC's uart_rx.S takes up to 24 bytes on IO0 (8N1); the sketch echoes them.
// ja: シリアルの受信をもう 1 つ: PIOC の uart_rx.S が IO0 で最大 24 バイトを受ける（8N1）。スケッチがそれを Serial に出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(uart_rx);
CH32RVPioc pioc;

// en: IO0's pad: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205.
// ja: IO0 の端子。CH32H417 は PA0、CH32X035 は PC7、CH32X033 は PC18（SWD と兼用）、CH32V205 は PB10。
#if defined(CH32RV_SERIES_CH32X035)
static const int IO0_PIN = PC7;
#elif defined(CH32RV_SERIES_CH32X033)
static const int IO0_PIN = PC18;   // en: its only IO0 pad, SWD's too / ja: 唯一の IO0 の端子で、SWD と兼用
#elif defined(CH32RV_SERIES_CH32V205)
static const int IO0_PIN = PB10;
#else
static const int IO0_PIN = PA0;
#endif

static const uint32_t BAUD = 115200;

// en: `cycles` as uart_rx.S's two-level wait: 3 * lo + 770 * hi (lo 1 .. 256, sent as 0 for 256).
// ja: `cycles` を uart_rx.S の 2 段の待ちに: 3 * lo + 770 * hi（lo は 1〜256。256 は 0 で渡す）。
static void split(int32_t cycles, uint8_t *out) {
  int32_t hi = cycles > 3 ? (cycles - 3) / 770 : 0;
  int32_t lo = (cycles - 770 * hi + 1) / 3;
  lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
  out[0] = (uint8_t)lo;
  out[1] = (uint8_t)hi;
}

// en: Starts taking `n` bytes (1 .. 24).
// ja: `n` バイト（1〜24）の受信を始める。
static void startReceive(uint8_t n) {
  const int32_t bit = (int32_t)((CH32RVPioc::clockHz() + BAUD / 2) / BAUD);
  uint8_t params[5] = {n};
  split(bit - 11, params + 1);                 // en: a bit / ja: 1 ビット
  split(bit + bit / 2 - 12, params + 3);       // en: the edge to bit 0's middle / ja: エッジから bit 0 の中央まで
  pioc.writeData(0, params, sizeof params);
  pioc.command(1);
}

void setup() {
  Serial.begin(115200);
  pioc.begin(PIOC_IMAGE_OF(uart_rx), IO0_PIN);
  startReceive(8);
}

void loop() {
  // en: Eight bytes at a time; after 2 s without them, whatever came.
  // ja: 8 バイトずつ。2 秒来なければ、来た分だけ。
  const int status = pioc.waitDone(2000);
  const uint8_t got = pioc.read(7);
  if (got) {
    uint8_t bytes[24];
    pioc.readData(8, bytes, got);
    Serial.print("PIOC got: ");
    Serial.write(bytes, got);
    Serial.println();
  }
  if (status != 0) {
    pioc.begin(PIOC_IMAGE_OF(uart_rx), IO0_PIN);   // en: stops the wait / ja: 待ちを止める
  }
  startReceive(8);
}
