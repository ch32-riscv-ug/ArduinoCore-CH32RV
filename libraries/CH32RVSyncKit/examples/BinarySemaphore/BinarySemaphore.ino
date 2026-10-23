// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::BinarySemaphore<> pressed;
static volatile uint32_t missed = 0;

static void onPress()
{
  // en: A second pending signal is refused rather than counted by the semaphore.
  // ja: 未処理の合図があれば拒否され、semaphore自体は回数を数えない。
  if (!pressed.give())
  {
    missed = missed + 1;
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("BinarySemaphore ready");
}

void loop()
{
  if (pressed.tryTake())
  {
    Serial.println("PRESSED");
  }
  if (missed)
  {
    Serial.print("MISSED ");
    Serial.println(missed);
    missed = 0;
  }
  // en: Deliberately slow the consumer to demonstrate missed signals. / ja: 合図の取りこぼしを見るため、受け手を意図的に遅くする。
  delay(1000);
}
