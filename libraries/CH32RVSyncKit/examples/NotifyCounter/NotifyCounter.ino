// en: Accumulate interrupt events and take their count from loop().
// ja: 割り込みの発生回数をため、loop()でまとめて取り出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::Notify<> presses;

static void onPress()
{
  // en: Record one occurrence only; the receiving side will do the printing later.
  // ja: ここでは一回の発生だけを記録し、表示は受け取った側で後から行う。
  presses.notify();
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
  Serial.println("NotifyCounter ready");
}

void loop()
{
  // en: Take and reset the pending count together.
  // ja: 保留中の回数を一度に取り出してリセットする。
  const uint32_t n = presses.tryTakeAll();
  if (n)
  {
    Serial.print("PRESSES since_last=");
    Serial.println(n);
  }
  delay(500);
}
