// en: One more serial transmitter: the PIOC's uart_tx.S sends text on IO0 (8N1) while the main core goes on.
// ja: シリアルの送信をもう 1 つ: PIOC の uart_tx.S が IO0 に文字列を送る（8N1）。その間メインのコアは別の仕事ができる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(uart_tx);
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

// en: Sends `n` bytes at BAUD, 24 at a time through the shared registers, and waits until the last stop bit is out.
// ja: `n` バイトを BAUD で送る。共有レジスタで 24 バイトずつ渡し、最後のストップビットが出るまで待つ。
static bool send(const void *data, size_t n) {
  // en: A bit is 3 * lo + 770 * hi + 11 of the PIOC's cycles (uart_tx.S).
  // ja: 1 ビットは PIOC の 3 * lo + 770 * hi + 11 サイクル（uart_tx.S）。
  const uint32_t cycles = (CH32RVPioc::clockHz() + BAUD / 2) / BAUD;
  const uint32_t hi = cycles > 11 + 3 ? (cycles - 11 - 3) / 770 : 0;
  uint32_t lo = (cycles - 11 - 770 * hi + 1) / 3;
  lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
  const uint8_t timing[2] = {(uint8_t)lo, (uint8_t)hi};   // en: 256 is 0 / ja: 256 は 0
  const uint8_t *p = static_cast<const uint8_t *>(data);
  while (n) {
    const uint8_t chunk = n > 24 ? 24 : (uint8_t)n;
    pioc.writeData(8, p, chunk);
    pioc.writeData(1, timing, sizeof timing);
    pioc.write(0, chunk);
    pioc.command(1);
    if (pioc.waitDone(1000) != 0) {
      return false;
    }
    p += chunk;
    n -= chunk;
  }
  return true;
}

void setup() {
  pioc.begin(PIOC_IMAGE_OF(uart_tx), IO0_PIN);
}

void loop() {
  char text[48];
  const int n = snprintf(text, sizeof text, "PIOC says hello, millis() = %lu\r\n", (unsigned long)millis());
  send(text, (size_t)n);
  delay(500);
}
