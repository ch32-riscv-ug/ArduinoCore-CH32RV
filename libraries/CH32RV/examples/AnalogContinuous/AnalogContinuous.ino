// en: Average an analog pin over frames the ADC fills by itself, with arduino-esp32's continuous ADC API.
// ja: arduino-esp32の連続ADCのAPIで、ADCが自分で埋めるフレームごとにアナログ端子を平均する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ADC1, A0 */

static const uint8_t PINS[] = {A0};          // en: change to your pins / ja: 自分の端子に変える
static const uint32_t CONVERSIONS_PER_PIN = 100;
static const uint32_t SAMPLING_HZ = 10000;   // en: conversions per second over all the pins

static volatile bool frameDone;

// en: Runs in the interrupt as each frame fills: only note it.
// ja: フレームが埋まるたびに割り込みで動く: 印を付けるだけ。
static void onFrame()
{
  frameDone = true;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  analogContinuousSetWidth(12);
  if (!analogContinuous(PINS, sizeof PINS, CONVERSIONS_PER_PIN, SAMPLING_HZ, onFrame))
  {
    Serial.println("ADC_CONTINUOUS_FAILED");
    return;
  }
  analogContinuousStart();
}

void loop()
{
  adc_continuous_result_t *result = nullptr;
  if (frameDone)
  {
    frameDone = false;
    if (analogContinuousRead(&result, 0))
    {
      for (size_t i = 0; i < sizeof PINS; i++)
      {
        Serial.printf("ADC_CONTINUOUS pin=%u raw=%d mV=%d\r\n", result[i].pin, result[i].avg_read_raw,
                      result[i].avg_read_mvolts);
      }
    }
  }
}
