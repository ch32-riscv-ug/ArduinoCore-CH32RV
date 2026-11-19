// en: An NEC infrared remote's frames received by the PIOC: nec_rx.S decodes what an IR receiver module (38 kHz,
//     such as a TSOP38238 or VS1838B) puts on IO0, one whole frame a command, while the main core goes on.
// ja: NEC 方式の赤外線リモコンのフレームを PIOC で受ける。nec_rx.S が、赤外線の受光モジュール（38 kHz。TSOP38238 や
//     VS1838B など）が IO0 に出す信号を、コマンド 1 つで 1 フレームずつ読み解く。その間メインのコアは別の仕事ができる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(nec_rx);
CH32RVPioc pioc;

// en: IO0's pad: PA0 on CH32H417, PC7 on CH32X035, PB10 on CH32V205.
// ja: IO0 の端子。CH32H417 は PA0、CH32X035 は PC7、CH32V205 は PB10。
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
static const int IO0_PIN = PC7;
#elif defined(CH32RV_SERIES_CH32V205)
static const int IO0_PIN = PB10;
#else
static const int IO0_PIN = PA0;
#endif

// en: nec_rx.S's tick: M x 256 turns of 3 cycles, and about 14 cycles around them (the call and the level's check).
// ja: nec_rx.S の 1 刻み: M × 256 回の 3 サイクル、まわりに約 14 サイクル（呼び出しと線の確かめ）。
static const uint32_t TURNS_CYCLES = 773;
static const uint32_t TICK_EXTRA = 14;

static uint8_t ticks[4];   // en: M, then the three thresholds as 256 - ticks / ja: M と、256 - 刻み数の 3 つのしきい値

static void setTicks() {
  const uint32_t clk = CH32RVPioc::clockHz();
  uint32_t m = (uint32_t)((uint64_t)clk * 75 / 1000000 / TURNS_CYCLES);   // en: about 75 us / ja: 約 75 µs
  m = m < 1 ? 1 : m > 255 ? 255 : m;
  const float tickUs = (float)(m * TURNS_CYCLES + TICK_EXTRA) * 1e6f / (float)clk;
  // en: Halfway between what NEC's times are: a 1's space 1687.5 us against a 0's 562.5; the leader's space 4.5 ms
  //     against a repeat's 2.25 ms; a leader's mark of 9 ms at least two thirds there.
  // ja: NEC の時間の中ほど: 1 の空白 1687.5 µs と 0 の 562.5 µs、リーダーの空白 4.5 ms と繰り返しの 2.25 ms。リーダーの
  //     9 ms の点灯は 3 分の 2 あれば認める。
  ticks[0] = (uint8_t)m;
  ticks[1] = (uint8_t)(256 - (int)(1125.0f / tickUs + 0.5f));
  ticks[2] = (uint8_t)(256 - (int)(3375.0f / tickUs + 0.5f));
  ticks[3] = (uint8_t)(256 - (int)(6000.0f / tickUs + 0.5f));
}

// en: One frame within timeoutMs: 0 a frame in `bits` (32, the first one in bit 0), 1 a repeat code, 2 broken off,
//     -1 nothing came.
// ja: timeoutMs の間に 1 フレーム: 0 は `bits` にフレーム（32 ビット、最初のビットが bit 0）、1 は繰り返しの符号、
//     2 は途中で切れた、-1 は何も来なかった。
static int receive(uint32_t timeoutMs, uint32_t &bits) {
  pioc.writeData(1, ticks, sizeof ticks);
  pioc.command(1);
  const int status = pioc.waitDone(timeoutMs);
  if (status < 0) {
    // en: Still waiting for a leader: begin again to stop it.
    // ja: まだリーダーを待っている。begin し直して止める。
    pioc.begin(PIOC_IMAGE_OF(nec_rx), IO0_PIN);
    return -1;
  }
  uint8_t b[4];
  pioc.readData(8, b, sizeof b);
  bits = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  return status;
}

void setup() {
  Serial.begin(115200);
  pioc.begin(PIOC_IMAGE_OF(nec_rx), IO0_PIN);
  setTicks();
}

void loop() {
  uint32_t bits;
  const int got = receive(1000, bits);
  if (got == 0) {
    const uint8_t a = bits, na = bits >> 8, c = bits >> 16, nc = bits >> 24;
    if ((uint8_t)~c != nc) {
      Serial.printf("bad frame %08lx (the command's inverse does not match)\r\n", (unsigned long)bits);
    } else if ((uint8_t)~a == na) {
      Serial.printf("address 0x%02x command 0x%02x\r\n", a, c);
    } else {
      Serial.printf("address 0x%04x (extended) command 0x%02x\r\n", a | na << 8, c);
    }
  } else if (got == 1) {
    Serial.println("repeat");
  } else if (got == 2) {
    Serial.println("broken frame");
  }
}
