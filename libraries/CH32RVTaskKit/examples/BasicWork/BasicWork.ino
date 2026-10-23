// en: Schedule LED blinking and UART input as two short work functions.
// ja: LED点滅とUART受信を、二つの短い仕事に分けて動かす。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

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
  // en: Consume available input and return; do not wait for the rest of a line.
  // ja: 今ある入力だけ処理して戻り、行の残りを待たない。
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
  // en: Attached work runs once initially, then when its wake condition is met.
  // ja: 登録直後は一度実行し、その後は起動条件に従う。
  app.attach(blink);
  app.attach(reader);
  blink.wakeEvery(every250);
  // en: The predicate checks input readiness; the reader runs later when runOnce() evaluates the work.
  // ja: 条件関数が受信の有無を調べ、仕事を実行するのはrunOnce()の中。
  reader.wakeWhen(uartHasBytes);
  Serial.println("BasicWork ready");
}

void loop()
{
  // en: Check all work once and return; no separate thread is created for the LED or reader.
  // ja: 各仕事を一度確認して戻る。LEDや受信処理のために別threadを作るわけではない。
  app.runOnce();
}
