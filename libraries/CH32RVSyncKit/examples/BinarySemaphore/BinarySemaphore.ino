// en: Pass one pending button signal from an interrupt to loop().
// ja: 割り込みからloop()へ、未処理のボタン通知を一つ渡す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

// en: Store only whether an event is pending; repeated events do not form a queue.
// ja: 未処理の通知があるかだけを保存する。複数回の発生を並べて保持するものではない。
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
  // en: The pull-up keeps an open button High; pressing it to GND makes the input Low.
  // ja: プルアップでボタン開放時をHighに保ち、GNDへ押すとLowになる。
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // en: Register an interrupt handler for the falling edge; the handler can interrupt loop().
  // ja: HighからLowへの変化に割り込み関数を登録する。この関数はloop()の途中にも動く。
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("BinarySemaphore ready");
}

void loop()
{
  // en: Take the pending signal without waiting; once taken, a later press can set it again.
  // ja: 待たずに通知を取り出す。取り出した後は、次の押下で再び通知を入れられる。
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
  // en: Deliberately slow the consumer to demonstrate missed signals.
  // ja: 合図の取りこぼしを見るため、受け手を意図的に遅くする。
  delay(1000);
}
