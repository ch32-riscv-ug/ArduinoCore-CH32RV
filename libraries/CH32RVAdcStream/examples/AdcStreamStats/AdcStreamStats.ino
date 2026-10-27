// en: Capture one analog pin continuously by DMA and print what each second's buffers held.
// ja: アナログ端子をDMAで連続して取り込み、1秒ごとにバッファの中身をまとめて表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ADC1 */

#include <CH32RVAdcStream.h>

static const pin_size_t INPUT_PIN = A0;      // en: change to your pin / ja: 自分の端子に変える
static const uint32_t WANTED_RATE = 100000;  // conversions per second
static const uint16_t SAMPLES = 256;         // per half

static CH32RVAdcStream adc;
static uint16_t buffer[2 * SAMPLES];

static uint32_t taken, sum, count;
static uint16_t low = 0xFFFF, high;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  // en: begin() measures the ADC's steps once and picks the fastest one at or below the rate.
  // ja: begin()はADCの速さの段を一度ずつ測り、指定以下で一番速い段を選ぶ。
  if (!adc.begin(INPUT_PIN, WANTED_RATE))
  {
    Serial.println("ADC_STREAM_BEGIN_FAILED");
    return;
  }
  Serial.printf("ADC_STREAM rate=%lu step=%u\r\n", (unsigned long)adc.sampleRate(), adc.sampleTimeStep());
  adc.startContinuous(buffer, SAMPLES);
}

void loop()
{
  // en: A full half is the sketch's until the next readBuffer(); use it and return quickly.
  // ja: 埋まった半分は次のreadBuffer()まで自分のもの。手早く使って戻る。
  if (const uint16_t *full = adc.readBuffer())
  {
    for (uint16_t i = 0; i < SAMPLES; i++)
    {
      const uint16_t v = full[i];
      sum += v;
      low = v < low ? v : low;
      high = v > high ? v : high;
    }
    count += SAMPLES;
    taken++;
  }

  static uint32_t last;
  if (millis() - last >= 1000)
  {
    last = millis();
    Serial.printf("ADC_STREAM rate=%lu samples/s=%lu halves=%lu overruns=%lu min=%u max=%u mean=%lu\r\n",
                  (unsigned long)adc.sampleRate(), (unsigned long)count, (unsigned long)taken,
                  (unsigned long)adc.overruns(), low, high,
                  (unsigned long)(count ? sum / count : 0));
    taken = sum = count = 0;
    low = 0xFFFF;
    high = 0;
  }
}
