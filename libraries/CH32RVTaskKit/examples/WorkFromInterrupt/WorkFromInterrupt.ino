// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVTaskKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Queue<uint32_t, 8> presses;

static void handlePresses()
{
  uint32_t t;
  uint32_t n = 0;
  // en: One scheduled run drains all pending events. / ja: 一回の起動で保留中のイベントをまとめて処理する。
  while (presses.tryReceive(t))
  {
    n++;
    Serial.print("PRESS ms=");
    Serial.println(t);
  }
  Serial.print("RUN handled=");
  Serial.println(n);
}

static CH32RVTaskKit::Work handler(handlePresses);

static void onPress()
{
  presses.trySend(millis());
  // en: Schedule work only; queued timestamps preserve individual events.
  // ja: ここでは実行予約だけを行い、個別イベントの時刻はQueueに残す。
  handler.post();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  app.attach(handler);
  Serial.println("WorkFromInterrupt ready");
}

void loop()
{
  app.runOnce();
  delay(300);
}
