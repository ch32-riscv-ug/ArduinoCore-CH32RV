// en: One more SPI transmitter, mode 0, MSB first: the PIOC's spi_tx.S clocks bytes out on IO0 (SCK) and IO1 (MOSI)
//     while the main core goes on; CS is an ordinary pin the sketch drives. For a shift register, an LED driver or a
//     display beside the hardware SPI.
// ja: SPI の送信をもう 1 つ（モード 0、MSB から）: PIOC の spi_tx.S が IO0（SCK）と IO1（MOSI）にバイトを送る。その間
//     メインのコアは別の仕事ができる。CS はスケッチが出す普通の端子。シフトレジスタ・LED のドライバ・表示器を、
//     ハードの SPI とは別に。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

PIOC_IMAGE(spi_tx);
CH32RVPioc pioc;

// en: IO0 / IO1 pads: PA0 / PC0 on CH32H417, PC7 / PC19 on CH32X035, PC18 / PC19 on CH32X033, PB10 / PB11 on CH32V205. CS: any pin.
// ja: IO0 / IO1 の端子。CH32H417 は PA0 / PC0、CH32X035 は PC7 / PC19、CH32X033 は PC18 / PC19、CH32V205 は PB10 / PB11。CS はどの端子でもよい。
#if defined(CH32RV_SERIES_CH32X035)
static const int SCK_PIN = PC7, MOSI_PIN = PC19, CS_PIN = PA1;
#elif defined(CH32RV_SERIES_CH32X033)
static const int SCK_PIN = PC18, MOSI_PIN = PC19, CS_PIN = PA1;   // en: PC18 is SWD's too / ja: PC18 は SWD と兼用
#elif defined(CH32RV_SERIES_CH32V205)
static const int SCK_PIN = PB10, MOSI_PIN = PB11, CS_PIN = PB2;
#else
static const int SCK_PIN = PA0, MOSI_PIN = PC0, CS_PIN = PA1;
#endif

static const uint32_t SCK_HZ = 1000000;

// en: The half period in turns of spi_tx.S's wait: a half period is 3 * turns + 6 of the PIOC's cycles.
// ja: spi_tx.S の待ちの回数にした半周期。半周期は PIOC の 3 * 回数 + 6 サイクル。
static uint8_t halfTurns(uint32_t hz)
{
  const int32_t half = (int32_t)((CH32RVPioc::clockHz() + hz) / (2 * hz));
  int32_t turns = (half - 6 + 1) / 3;
  turns = turns < 1 ? 1 : turns > 256 ? 256 : turns;
  return (uint8_t)turns;                  // en: 256 is 0 / ja: 256 は 0
}

// en: Sends n bytes with CS low around them, 24 at a time through the shared registers.
// ja: n バイトを、前後に CS を Low にして送る。共有レジスタで 24 バイトずつ。
static bool transfer(const uint8_t *data, size_t n)
{
  const uint8_t half = halfTurns(SCK_HZ);
  digitalWrite(CS_PIN, LOW);
  bool ok = true;
  while (ok && n) {
    const uint8_t chunk = n > 24 ? 24 : (uint8_t)n;
    pioc.writeData(8, data, chunk);
    pioc.writeData(1, &half, 1);
    pioc.write(0, chunk);
    pioc.command(1);
    ok = pioc.waitDone(1000) == 0;
    data += chunk;
    n -= chunk;
  }
  digitalWrite(CS_PIN, HIGH);
  return ok;
}

void setup()
{
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);
  pioc.begin(PIOC_IMAGE_OF(spi_tx), SCK_PIN, MOSI_PIN);
}

void loop()
{
  // en: A counter onto two chained 74HC595s, for one.
  // ja: たとえば 74HC595 を 2 つつないだものへカウンタを。
  static uint16_t count;
  const uint8_t bytes[2] = {(uint8_t)(count >> 8), (uint8_t)count};
  transfer(bytes, sizeof bytes);
  count++;
  delay(100);
}
