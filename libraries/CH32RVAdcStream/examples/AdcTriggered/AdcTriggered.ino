// en: Convert an analog pin on a timer's tick, then on another pin's rising edge, and print what came in.
// ja: アナログ端子をタイマーの刻みで、次に別の端子の立ち上がりで変換し、届いたものを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ADC1, A0 */

#include <CH32RVAdcStream.h>

static const pin_size_t INPUT_PIN = A0;      // en: change to your pin / ja: 自分の端子に変える
static const uint32_t TIMER_RATE = 1000;     // en: conversions per second / ja: 1 秒あたりの変換の数
static const uint16_t SAMPLES = 64;          // per half

static CH32RVAdcStream adc;
static uint16_t buffer[2 * SAMPLES];
static pin_size_t triggerPin;
static bool onEdge;
static uint32_t shown;

// en: The first pad that can start the ADC by its edge (which ones depends on the chip).
// ja: 縁でADCを動かせる最初の端子（どれかは石による）。
static pin_size_t firstTriggerPin()
{
  for (pin_size_t pin = 0; pin < NUM_DIGITAL_PINS; pin++)
  {
    if (digitalPinIsValid(pin) && pin != INPUT_PIN && CH32RVAdcStream::isExternalTriggerPin(pin))
    {
      return pin;
    }
  }
  return 0xFF;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  // en: A timer ticks TIMER_RATE times a second; each tick converts INPUT_PIN once.
  // ja: タイマーが毎秒TIMER_RATE回刻み、刻みごとにINPUT_PINを1回変換する。
  if (!adc.beginTimer(INPUT_PIN, TIMER_RATE))
  {
    Serial.println("ADC_TIMER_BEGIN_FAILED");
    return;
  }
  Serial.printf("ADC_TIMER rate=%lu timer=TIM%u step=%u\r\n", (unsigned long)adc.sampleRate(),
                adc.triggerTimer(), adc.sampleTimeStep());
  adc.startContinuous(buffer, SAMPLES);
  triggerPin = firstTriggerPin();
}

void loop()
{
  static uint32_t last;
  if (!onEdge && millis() - last >= 1000)
  {
    // en: conversions() counts the rounds since startContinuous(): about TIMER_RATE a second.
    // ja: conversions()はstartContinuous()からの変換の数: 毎秒ほぼTIMER_RATE。
    last = millis();
    const uint32_t n = adc.conversions();
    Serial.printf("ADC_TIMER conversions=%lu (+%lu) last=%u\r\n", (unsigned long)n, (unsigned long)(n - shown),
                  buffer[(n + 2 * SAMPLES - 1) % (2 * SAMPLES)]);
    shown = n;
    if (n >= 5 * TIMER_RATE && triggerPin != 0xFF)
    {
      // en: Now one conversion per rising edge of triggerPin.
      // ja: ここからtriggerPinの立ち上がりごとに1回変換する。
      pinMode(triggerPin, INPUT_PULLDOWN);
      if (adc.beginExternal(INPUT_PIN, triggerPin, RISING))
      {
        adc.startContinuous(buffer, SAMPLES);
        onEdge = true;
        shown = 0;
        Serial.printf("ADC_EDGE trigger pin=%u\r\n", triggerPin);
      }
    }
  }
  if (onEdge)
  {
    const uint32_t n = adc.conversions();
    for (; shown < n; shown++)
    {
      Serial.printf("ADC_EDGE %lu value=%u\r\n", (unsigned long)shown, buffer[shown % (2 * SAMPLES)]);
    }
  }
}
