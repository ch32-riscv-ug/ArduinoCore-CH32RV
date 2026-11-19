// en: WS2812 (NeoPixel) LEDs driven by the PIOC: ws2812.S sends the frame while the main core goes on.
// ja: PIOC で WS2812（NeoPixel）を光らせる。ws2812.S がフレームを送り、その間メインのコアは別の仕事ができる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(ws2812);
CH32RVPioc pioc;

// en: IO0's pad: PA0 on CH32H417, PC7 on CH32X035 (PC18 is SWD's too), PB10 on CH32V205.
// ja: IO0 の端子。CH32H417 は PA0、CH32X035 は PC7（PC18 は SWD と兼用）、CH32V205 は PB10。
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
static const int IO0_PIN = PC7;
#elif defined(CH32RV_SERIES_CH32V205)
static const int IO0_PIN = PB10;
#else
static const int IO0_PIN = PA0;
#endif

static const int LEDS = 30;
static uint8_t pixels[LEDS * 3];          // G, R, B per LED (WS2812's order)

// en: Turns of the program's 3-cycle loop for `ns` nanoseconds, less `fixed` cycles already spent.
// ja: `ns` ナノ秒に当たる、プログラムの 3 サイクルのループの回数（`fixed` サイクルは別にかかる分）。
static uint8_t turns(uint32_t ns, uint32_t fixed) {
  const uint32_t cycles = (uint32_t)((uint64_t)ns * CH32RVPioc::clockHz() / 1000000000u);
  const int32_t t = ((int32_t)cycles - (int32_t)fixed + 1) / 3;
  return (uint8_t)(t < 1 ? 1 : t > 255 ? 255 : t);
}

static void show() {
  // en: The bytes into the PIOC's SRAM at 0x400 (the PIOC starts again), then the count and the timing
  // en: (ws2812.S's head counts the cycles).
  // ja: バイト列を PIOC の SRAM の 0x400 に写す（PIOC は最初から走り直す）。次に数とタイミングを渡す
  // ja: （サイクル数は ws2812.S の先頭）。
  pioc.loadSram(0x400, pixels, sizeof pixels);
  const uint8_t h0 = turns(350, 4);                  // a 0: high 350 ns
  const uint8_t h1 = turns(800, 6 + 3 * h0) ;        // a 1: high 800 ns
  const uint8_t lo = turns(1250, 13 + 3 * (h0 + h1)); // a bit: 1.25 us
  const uint8_t last = lo > 6 ? lo - 5 : 1;            // a byte's last bit: the same, the next byte's set-up in it
  const uint8_t params[6] = {(uint8_t)sizeof pixels, (uint8_t)(sizeof pixels >> 8), h0, h1, lo, last};
  pioc.writeData(0, params, sizeof params);
  pioc.command(1);
  pioc.waitDone(100);
  delayMicroseconds(300);                  // en: the latch / ja: ラッチ
}

void setup() {
  pioc.begin(PIOC_IMAGE_OF(ws2812), IO0_PIN);
}

void loop() {
  // en: A rainbow that walks along the strip.
  // ja: 帯の上を動く虹。
  static uint8_t start;
  for (int i = 0; i < LEDS; i++) {
    const uint8_t h = (uint8_t)(start + i * 256 / LEDS);
    const uint8_t r = h < 85 ? 255 - h * 3 : h < 170 ? 0 : (h - 170) * 3;
    const uint8_t g = h < 85 ? h * 3 : h < 170 ? 255 - (h - 85) * 3 : 0;
    const uint8_t b = h < 85 ? 0 : h < 170 ? (h - 85) * 3 : 255 - (h - 170) * 3;
    pixels[3 * i] = g / 8;
    pixels[3 * i + 1] = r / 8;
    pixels[3 * i + 2] = b / 8;
  }
  show();
  start += 2;
  delay(20);
}
