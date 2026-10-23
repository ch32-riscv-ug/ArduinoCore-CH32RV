// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVTaskKit.h>

static const int LED_PIN = PA1;

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Period every250(250);
static bool ledOn = false;

static void blinkStep()
{
  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

static void readStep()
{
  // en: Consume available input and return; do not wait for the rest of a line. / ja: 今ある入力だけ処理して戻り、行の残りを待たない。
  while (Serial.available())
  {
    const int c = Serial.read();
    if (c == '\n')
    {
      Serial.println("LINE");
    }
  }
}

static bool uartHasBytes()
{
  return Serial.available() > 0;
}

static CH32RVTaskKit::Work blink(blinkStep);
static CH32RVTaskKit::Work reader(readStep);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(LED_PIN, OUTPUT);
  // en: Attached work runs once initially, then when its wake condition is met. / ja: 登録直後は一度実行し、その後は起動条件に従う。
  app.attach(blink);
  app.attach(reader);
  blink.wakeEvery(every250);
  reader.wakeWhen(uartHasBytes);
  Serial.println("BasicWork ready");
}

void loop()
{
  app.runOnce();
}
