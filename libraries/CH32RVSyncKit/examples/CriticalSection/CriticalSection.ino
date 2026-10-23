// en: Read ISR-written counters and timestamps as one consistent snapshot.
// ja: ISRが書いた回数と時刻を、整合する一組として読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static volatile uint32_t pressCount = 0;
static volatile uint32_t lastPressMs = 0;
// en: This value spans multiple machine words, so an interrupt during a read could split its state.
// ja: この値は複数wordにまたがるため、読み取り途中の割り込みで新旧の値が混ざり得る。
static volatile uint64_t sumOfMicros = 0;

static void onPress()
{
  pressCount = pressCount + 1;
  lastPressMs = millis();
  sumOfMicros = sumOfMicros + micros();
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
  Serial.println("CriticalSection ready");
}

void loop()
{
  // en: Copy the shared values together, then use these ordinary local variables for the slow output.
  // ja: 共有値をまとめてローカルへコピーし、時間のかかる表示にはそのコピーを使う。
  uint32_t count, last;
  uint64_t sum;
  {
    // en: Copy the whole snapshot; scope exit restores the previous interrupt state.
    // ja: 一組の値をまとめてコピーし、スコープ終了で元の割り込み状態に戻す。
    CH32RVSyncKit::CriticalSection cs;
    count = pressCount;
    last = lastPressMs;
    sum = sumOfMicros;
  }

  // en: Print from local copies after leaving the protected scope.
  // ja: 保護区間を抜けてからローカルのコピーを表示する。
  Serial.print("PRESSES count=");
  Serial.print(count);
  Serial.print(" last_ms=");
  Serial.print(last);
  Serial.print(" sum_us_low=");
  Serial.println((unsigned long)(sum & 0xFFFFFFFFu));
  delay(1000);
}
