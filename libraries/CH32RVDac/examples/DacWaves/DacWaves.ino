// en: DAC1 makes a 1 kHz sine from 64 samples, then its own triangle, then noise, two seconds each, round and round.
// ja: DAC1 が 64 サンプルの 1 kHz の正弦波、DAC 自身の三角波、ノイズを 2 秒ずつ順に出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_DAC1_DMA */

#include <CH32RVDac.h>

CH32RVDac dac(1);
uint16_t sine[64];

void setup() {
  Serial.begin(115200);
  // en: 12-bit samples around mid-scale (0 .. 4095).
  // ja: 中央を軸にした 12 bit のサンプル（0〜4095）。
  for (int i = 0; i < 64; i++) {
    sine[i] = (uint16_t)(2048 + 2000 * sin(2 * PI * i / 64));
  }
  dac.begin();
}

void loop() {
  // en: 64 samples at 64 kHz: 1 kHz. The DMA feeds them; the CPU is free.
  // ja: 64 サンプルを 64 kHz で: 1 kHz。DMA が送るので CPU は空いている。
  dac.play(sine, 64, 64000);
  Serial.print("sine: steps/s ");
  Serial.println(dac.stepRate());
  delay(2000);

  // en: 10 bits from 1024: 1024 .. 2047 and back, a step every 5 us (about 98 Hz).
  // ja: 1024 から 10 bit: 1024〜2047 を往復、5 us ごとに 1 段（約 98 Hz）。
  dac.triangle(10, 200000, 1024);
  Serial.print("triangle: steps/s ");
  Serial.println(dac.stepRate());
  delay(2000);

  // en: 8 bits of the converter's noise above 2048, a new value 100000 times a second.
  // ja: 2048 の上に変換器のノイズ 8 bit、毎秒 100000 回。
  dac.noise(8, 100000, 2048);
  Serial.print("noise: steps/s ");
  Serial.println(dac.stepRate());
  delay(2000);
}
