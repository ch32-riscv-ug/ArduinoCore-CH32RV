// en: The GPHA against the CPU: a 160 x 120 RGB565 frame filled, and an ARGB8888 picture copied into it, by each.
// ja: GPHA と CPU を比べる: 160 x 120 の RGB565 の画面を塗りつぶし、ARGB8888 の絵をそこへ写す。それぞれで時間を測る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_GPHA_BASE */

#include <CH32RVGpha.h>

static const uint16_t W = 160, H = 120;
alignas(4) static uint16_t frame[W * H];
alignas(4) static uint32_t picture[64 * 64];
CH32RVGpha gpha;

void setup() {
  Serial.begin(115200);
  delay(1000);
  for (int i = 0; i < 64 * 64; i++) {
    picture[i] = 0xFF000000u | (uint32_t)(i * 4) << 16 | (uint32_t)(i % 64 * 4) << 8 | 0x80;
  }
  CH32RVGpha::Surface screen{frame, CH32RVGpha::RGB565, W};
  CH32RVGpha::Surface image{picture, CH32RVGpha::ARGB8888, 64};

  uint32_t t0 = micros();
  gpha.fill(screen, 0, 0, W, H, 0xFF2040C0);
  const uint32_t gphaFill = micros() - t0;
  t0 = micros();
  for (uint32_t i = 0; i < (uint32_t)W * H; i++) {
    frame[i] = 0x2218;
  }
  const uint32_t cpuFill = micros() - t0;

  t0 = micros();
  gpha.copy(image, 0, 0, screen, 48, 28, 64, 64);
  const uint32_t gphaCopy = micros() - t0;
  t0 = micros();
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 64; x++) {
      const uint32_t c = picture[y * 64 + x];
      frame[(y + 28) * W + x + 48] = (uint16_t)(((c >> 19) & 0x1F) << 11 | ((c >> 10) & 0x3F) << 5 | ((c >> 3) & 0x1F));
    }
  }
  const uint32_t cpuCopy = micros() - t0;

  Serial.printf("fill 160x120 RGB565: GPHA %lu us, CPU %lu us\r\n", (unsigned long)gphaFill, (unsigned long)cpuFill);
  Serial.printf("copy 64x64 ARGB8888 -> RGB565: GPHA %lu us, CPU %lu us\r\n", (unsigned long)gphaCopy,
                (unsigned long)cpuCopy);
}

void loop() {
}
