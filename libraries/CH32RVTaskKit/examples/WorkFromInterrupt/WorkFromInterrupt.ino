// en: Post work from an interrupt and process queued events in loop().
// ja: 割り込みで仕事を予約し、loop()で保留イベントを処理する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTaskKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Queue<uint32_t, 8> presses;

static void handlePresses()
{
  uint32_t t;
  uint32_t n = 0;
  // en: One scheduled run drains all pending events.
  // ja: 一回の起動で保留中のイベントをまとめて処理する。
  while (presses.tryReceive(t))
  {
    n++;
    Serial.print("PRESS ms=");
    Serial.println(t);
  }
  Serial.print("RUN handled=");
  Serial.println(n);
}

// en: Bind the function without running it; post() and runOnce() control when it is called.
// ja: 関数を登録するだけで、この宣言では実行しない。post()とrunOnce()で実行を制御する。
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
  // en: The pull-up keeps an open button High; pressing it to GND makes the input Low.
  // ja: プルアップでボタン開放時をHighに保ち、GNDへ押すとLowになる。
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // en: Register an interrupt handler for the falling edge; the handler can interrupt loop().
  // ja: HighからLowへの変化に割り込み関数を登録する。この関数はloop()の途中にも動く。
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  app.attach(handler);
  Serial.println("WorkFromInterrupt ready");
}

void loop()
{
  app.runOnce();
  delay(300);
}
