// en: Queue button timestamps in an interrupt and process them in loop().
// ja: ボタンを押した時刻を割り込みでQueueへ入れ、loop()で処理する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

// en: Keep up to eight timestamps in order; trySend() copies the value into the queue.
// ja: 時刻を順番に8件まで保持する。trySend()が値そのものをQueueへコピーする。
static CH32RVSyncKit::Queue<uint32_t, 8> presses;
static volatile uint32_t dropped = 0;

static void onPress()
{
  // en: Never wait in the ISR; count a full-queue refusal and return.
  // ja: ISRでは待たず、Queue満杯の拒否を数えて戻る。
  if (!presses.trySend(millis()))
  {
    dropped = dropped + 1;
  }
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
  Serial.println("QueueFromInterrupt ready");
}

void loop()
{
  uint32_t t;
  // en: Drain queued values in arrival order outside the ISR.
  // ja: ISRの外で、到着順にQueueを読み出す。
  while (presses.tryReceive(t))
  {
    Serial.print("PRESS ms=");
    Serial.print(t);
    Serial.print(" queued=");
    Serial.println(presses.count());
  }
  if (dropped)
  {
    Serial.print("DROPPED ");
    Serial.println(dropped);
    dropped = 0;
  }
  // en: This deliberately slow consumer makes queued and dropped events visible in the demonstration.
  // ja: 受け手を意図的に遅くし、Queueにたまる様子や満杯時の拒否を観察する。
  delay(200);
}
