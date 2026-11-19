// en: A 1-Wire master from the PIOC: onewire.S makes the reset, write and read slots on IO0 with their microsecond
//     timing, which interrupts on the main core cannot stretch. Reads a DS18B20's temperature every second.
// ja: PIOC で 1-Wire のマスター。onewire.S が IO0 にリセット・書き込み・読み出しのスロットをマイクロ秒の時間で作る。
//     メインのコアの割り込みで時間が延びない。DS18B20 の温度を 1 秒ごとに読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(onewire);
CH32RVPioc pioc;

// en: IO0's pad: PA0 on CH32H417, PC7 on CH32X035, PB10 on CH32V205. A 4.7 kOhm pull-up to 3.3 V on it.
// ja: IO0 の端子。CH32H417 は PA0、CH32X035 は PC7、CH32V205 は PB10。3.3 V への 4.7 kΩ のプルアップを付ける。
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
static const int IO0_PIN = PC7;
#elif defined(CH32RV_SERIES_CH32V205)
static const int IO0_PIN = PB10;
#else
static const int IO0_PIN = PA0;
#endif

static uint8_t usTurns;

// en: The turns of onewire.S's wait that make one microsecond: 3 * turns + 4 cycles.
// ja: onewire.S の待ちで 1 µs になる回数: 3 * 回数 + 4 サイクル。
static uint8_t microsecondTurns() {
  int32_t turns = (int32_t)((CH32RVPioc::clockHz() / 1000000 - 4 + 1) / 3);
  return (uint8_t)(turns < 1 ? 1 : turns > 255 ? 255 : turns);
}

static int run(uint8_t command, uint8_t count) {
  pioc.write(0, count);
  pioc.write(1, usTurns);
  pioc.command(command);
  return pioc.waitDone(100);
}

// en: A reset: true when a device answered with its presence pulse.
// ja: リセット。デバイスが存在のパルスで答えたら true。
static bool oneWireReset() {
  return run(1, 0) == 0;
}

static bool oneWireWrite(const uint8_t *data, uint8_t n) {
  while (n) {
    const uint8_t k = n > 24 ? 24 : n;
    pioc.writeData(8, data, k);
    if (run(2, k) != 0) {
      return false;
    }
    data += k;
    n -= k;
  }
  return true;
}

static bool oneWireRead(uint8_t *data, uint8_t n) {
  while (n) {
    const uint8_t k = n > 24 ? 24 : n;
    if (run(3, k) != 0) {
      return false;
    }
    pioc.readData(8, data, k);
    data += k;
    n -= k;
  }
  return true;
}

// en: Dallas / Maxim CRC-8 (x^8 + x^5 + x^4 + 1), as the scratchpad's last byte.
// ja: Dallas / Maxim の CRC-8（x^8 + x^5 + x^4 + 1）。スクラッチパッドの最後のバイト。
static uint8_t crc8(const uint8_t *data, uint8_t n) {
  uint8_t crc = 0;
  while (n--) {
    uint8_t b = *data++;
    for (uint8_t i = 0; i < 8; i++) {
      const uint8_t mix = (crc ^ b) & 1;
      crc >>= 1;
      if (mix) {
        crc ^= 0x8C;
      }
      b >>= 1;
    }
  }
  return crc;
}

void setup() {
  Serial.begin(115200);
  pioc.begin(PIOC_IMAGE_OF(onewire), IO0_PIN);
  pioc.setOpenDrain(0);   // en: the line is pulled low or let go / ja: 線は Low に引くか放すだけ
  usTurns = microsecondTurns();
}

void loop() {
  static const uint8_t convert[] = {0xCC, 0x44};   // en: Skip ROM, Convert T / ja: Skip ROM、Convert T
  static const uint8_t readPad[] = {0xCC, 0xBE};   // en: Skip ROM, Read Scratchpad / ja: Skip ROM、Read Scratchpad
  if (!oneWireReset()) {
    Serial.println("no device (no presence pulse)");
    delay(1000);
    return;
  }
  oneWireWrite(convert, sizeof convert);
  delay(750);                                       // en: 12 bits take up to 750 ms / ja: 12 ビットは最長 750 ms
  uint8_t pad[9];
  if (!oneWireReset() || !oneWireWrite(readPad, sizeof readPad) || !oneWireRead(pad, sizeof pad)) {
    Serial.println("the device went away");
  } else if (crc8(pad, 8) != pad[8]) {
    Serial.println("CRC error");
  } else {
    const int16_t raw = (int16_t)(pad[0] | pad[1] << 8);
    Serial.print(raw / 16.0f, 4);
    Serial.println(" C");
  }
  delay(250);
}
