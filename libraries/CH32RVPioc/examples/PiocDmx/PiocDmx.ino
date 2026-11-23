// en: A DMX512 controller: the PIOC's dmx_tx.S sends a universe of 32 channels on IO0 (to an RS-485 driver), 40 times
//     a second, while the main core works out the next values.
// ja: DMX512 のコントローラー: PIOC の dmx_tx.S が 32 チャネルのユニバースを IO0 に送る（RS-485 のドライバーへ）。
//     1 秒に 40 回。その間メインのコアは次の値を計算できる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(dmx_tx);
CH32RVPioc pioc;

// en: IO0's pad: PA0 on CH32H417, PC7 on CH32X035, PC18 on CH32X033 (SWD's too), PB10 on CH32V205.
// ja: IO0 の端子。CH32H417 は PA0、CH32X035 は PC7、CH32X033 は PC18（SWD と兼用）、CH32V205 は PB10。
#if defined(CH32RV_SERIES_CH32X035)
static const int IO0_PIN = PC7;
#elif defined(CH32RV_SERIES_CH32X033)
static const int IO0_PIN = PC18;
#elif defined(CH32RV_SERIES_CH32V205)
static const int IO0_PIN = PB10;
#else
static const int IO0_PIN = PA0;
#endif

static const uint16_t CHANNELS = 32;
static uint8_t universe[1 + CHANNELS];   // en: [0] the start code (0), then channels 1 .. 32 / ja: [0] はスタートコード（0）

// en: Sends the universe as one packet: the break and the mark after it, then the bytes, 24 at a time.
// ja: ユニバースを 1 つのパケットで送る。ブレークとその後のマーク、続いてバイトを 24 個ずつ。
static bool sendPacket() {
  // en: A bit is 3 * lo + 770 * hi + 11 of the PIOC's cycles (dmx_tx.S): 4 us at 250 kbit/s.
  // ja: 1 ビットは PIOC の 3 * lo + 770 * hi + 11 サイクル（dmx_tx.S）。250 kbit/s で 4 us。
  const uint32_t cycles = (CH32RVPioc::clockHz() + 125000) / 250000;
  const uint32_t hi = cycles > 11 + 3 ? (cycles - 11 - 3) / 770 : 0;
  uint32_t lo = (cycles - 11 - 770 * hi + 1) / 3;
  lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
  const uint8_t timing[2] = {(uint8_t)lo, (uint8_t)hi};
  for (size_t at = 0; at < sizeof universe; at += 24) {
    const uint8_t chunk = sizeof universe - at > 24 ? 24 : (uint8_t)(sizeof universe - at);
    const uint8_t flags = at == 0 ? 1 : 0;   // en: the break before the first chunk / ja: 最初の塊の前にブレーク
    pioc.writeData(8, universe + at, chunk);
    pioc.writeData(1, timing, sizeof timing);
    pioc.writeData(5, &flags, 1);
    pioc.write(0, chunk);
    pioc.command(1);
    if (pioc.waitDone(100) != 0) {
      return false;
    }
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  if (!pioc.begin(PIOC_IMAGE_OF(dmx_tx), IO0_PIN)) {
    Serial.println("no PIOC on that pad");
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  // en: a slow chase: each channel's level a triangle, shifted along the channels
  // ja: ゆっくりした流れ: 各チャネルの明るさを三角波にし、チャネルごとにずらす
  const uint32_t t = millis() / 8;
  for (uint16_t c = 0; c < CHANNELS; c++) {
    const uint32_t phase = (t + c * 16) & 511;
    universe[1 + c] = (uint8_t)(phase < 256 ? phase : 511 - phase);
  }
  if (!sendPacket()) {
    Serial.println("PIOC did not answer");
  }
  delay(25);
}
