// en: Blink periodically while checking an input deadline without waiting.
// ja: 周期的に点滅しながら、入力期限を待たずに確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int LED_PIN = PA1;

static CH32RVSyncKit::Period blink(250);
static CH32RVSyncKit::Deadline answer;
static bool waiting = false;
static bool ledOn = false;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("DeadlinePeriod ready");
}

void loop()
{
  // en: Checking a schedule does not wait for it.
  // ja: 予定時刻の確認だけで、そこまで待機はしない。
  if (blink.due())
  {
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  }
  if (blink.missed())
  {
    Serial.print("LATE skipped=");
    Serial.println(blink.missed());
    blink.clearMissed();
  }

  if (!waiting)
  {
    Serial.println("TYPE something within 5 s");
    // en: Start a fresh input deadline now; later loop() calls check it while the LED continues.
    // ja: 今から入力期限を設定する。LEDを動かしながら、次以降のloop()で期限を確認する。
    answer.arm(5000);
    waiting = true;
  }
  // en: Any received byte counts as an answer here, not a complete line.
  // ja: ここでは行の完成ではなく、1 byteでも受信すれば応答とする。
  else if (Serial.available())
  {
    while (Serial.available())
    {
      Serial.read();
    }
    Serial.print("THANKS spare_ms=");
    Serial.println(answer.remaining());
    waiting = false;
  }
  else if (answer.expired())
  {
    Serial.println("TOO_SLOW");
    waiting = false;
  }
}
