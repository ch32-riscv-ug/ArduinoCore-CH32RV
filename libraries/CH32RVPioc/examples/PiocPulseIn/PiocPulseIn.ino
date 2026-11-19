// en: pulseIn() by the PIOC: pulse_in.S counts one period of the signal on IO0, the main core's interrupts aside.
// ja: PIOC で pulseIn(): pulse_in.S が IO0 の信号の 1 周期を数える。メインのコアの割込みに揺らされない。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(pulse_in);
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

// en: One period: the high and the low in nanoseconds; false when no edge came within timeoutMs.
// ja: 1 周期の High と Low をナノ秒で。timeoutMs の間にエッジが来なければ false。
static bool measure(uint32_t timeoutMs, uint32_t &highNs, uint32_t &lowNs) {
  pioc.command(1);
  if (pioc.waitDone(timeoutMs) != 0) {
    // en: Still waiting for an edge: begin again to stop it.
    // ja: まだエッジを待っている。begin し直して止める。
    pioc.begin(PIOC_IMAGE_OF(pulse_in), IO0_PIN);
    return false;
  }
  uint8_t c[6];
  pioc.readData(1, c, sizeof c);
  const uint32_t high = c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16;
  const uint32_t low = c[3] | (uint32_t)c[4] << 8 | (uint32_t)c[5] << 16;
  // en: Each turn is 8 of the PIOC's cycles.
  // ja: 1 回は PIOC の 8 サイクル。
  highNs = (uint32_t)((uint64_t)high * 8u * 1000000000u / CH32RVPioc::clockHz());
  lowNs = (uint32_t)((uint64_t)low * 8u * 1000000000u / CH32RVPioc::clockHz());
  return true;
}

void setup() {
  Serial.begin(115200);
  pioc.begin(PIOC_IMAGE_OF(pulse_in), IO0_PIN);
}

void loop() {
  uint32_t highNs, lowNs;
  if (measure(1000, highNs, lowNs)) {
    Serial.print("high ");
    Serial.print(highNs);
    Serial.print(" ns, low ");
    Serial.print(lowNs);
    Serial.print(" ns, ");
    Serial.print(1e9 / (highNs + lowNs), 1);
    Serial.println(" Hz");
  } else {
    Serial.println("no signal");
  }
  delay(500);
}
