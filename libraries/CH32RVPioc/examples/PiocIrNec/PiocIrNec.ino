// en: An NEC infrared remote's frames from the PIOC: ir_nec.S drives an IR LED on IO0 with the 38 kHz carrier.
// ja: PIOC で NEC 方式の赤外線リモコンのフレームを送る。ir_nec.S が IO0 の赤外線 LED を 38 kHz の搬送波で光らせる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(ir_nec);
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

static const uint32_t CARRIER_HZ = 38000;

// en: `cycles` as ir_nec.S's two-level wait: 3 * lo + 770 * hi (lo 1 .. 256, sent as 0 for 256).
// ja: `cycles` を ir_nec.S の 2 段の待ちに: 3 * lo + 770 * hi（lo は 1〜256。256 は 0 で渡す）。
static void split(int32_t cycles, uint8_t *out) {
  int32_t hi = cycles > 3 ? (cycles - 3) / 770 : 0;
  int32_t lo = (cycles - 770 * hi + 1) / 3;
  lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
  out[0] = (uint8_t)lo;
  out[1] = (uint8_t)hi;
}

// en: One frame: an address (above 0xFF: NEC's extended 16-bit address) and a command.
// ja: 1 フレーム: アドレス（0xFF より上は NEC の拡張の 16 ビットのアドレス）とコマンド。
static bool sendNec(uint16_t address, uint8_t command) {
  const int32_t period = (int32_t)((CH32RVPioc::clockHz() + CARRIER_HZ / 2) / CARRIER_HZ);
  const int32_t high = period / 3;
  uint8_t frame[8];
  frame[0] = (uint8_t)address;
  frame[1] = address > 0xFF ? (uint8_t)(address >> 8) : (uint8_t)~address;
  frame[2] = command;
  frame[3] = (uint8_t)~command;
  // en: A period is (3 * d4 + 770 * d5) + (3 * d6 + 770 * d7) + 25 cycles, high for the first part + 11.
  // ja: 1 周期は (3 * d4 + 770 * d5) + (3 * d6 + 770 * d7) + 25 サイクル。High は前半 + 11。
  split(high - 11, frame + 4);
  split(period - 25 - (3 * (frame[4] ? frame[4] : 256) + 770 * frame[5]), frame + 6);
  pioc.writeData(0, frame, sizeof frame);
  pioc.command(1);
  return pioc.waitDone(200) == 0;
}

void setup() {
  pioc.begin(PIOC_IMAGE_OF(ir_nec), IO0_PIN);
}

void loop() {
  // en: Address 0x00, command 0x45 ("power" on many small remotes), every second.
  // ja: アドレス 0x00、コマンド 0x45（小さなリモコンの多くで電源）を 1 秒ごとに。
  sendNec(0x00, 0x45);
  delay(1000);
}
