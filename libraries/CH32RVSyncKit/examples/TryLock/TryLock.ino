// en: Let an interrupt skip a buffer while loop() is updating it in stages.
// ja: loop()が分割更新しているバッファを、割り込み側では触らずに飛ばす。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::TryLock<> bufferLock;
static uint8_t buffer[16];
static volatile uint32_t isrWrites = 0;
static volatile uint32_t isrSkips = 0;
static uint8_t filledUpTo = 0;

static void onPress()
{
  // en: The ISR must skip on lock failure; waiting would prevent loop() from releasing it.
  // ja: ISRはロック失敗時に飛ばす。待つとloop()が解放できなくなる。
  CH32RVSyncKit::TryLock<>::Guard g(bufferLock);
  if (!g.locked())
  {
    isrSkips = isrSkips + 1;
    return;
  }
  buffer[0] = 0xEE;
  isrWrites = isrWrites + 1;
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
  Serial.println("TryLock ready");
}

void loop()
{
  // en: Keep ownership across rounds until all partial writes are complete.
  // ja: 分割した書き込みが完了するまで、巡回をまたいで所有する。
  if (filledUpTo == 0 && !bufferLock.tryLock())
  {
    return;
  }
  for (uint8_t i = 0; i < 4; i++)
  {
    buffer[filledUpTo + i] = (uint8_t)(filledUpTo + i);
  }
  filledUpTo += 4;
  if (filledUpTo == sizeof buffer)
  {
    filledUpTo = 0;
    // en: Release only when the entire staged update is complete, so an ISR never sees half-written data.
    // ja: 分割更新がすべて終わってから解放し、ISRに更新途中の内容を見せない。
    bufferLock.unlock();
    Serial.print("DONE isr_writes=");
    Serial.print(isrWrites);
    Serial.print(" isr_skips=");
    Serial.println(isrSkips);
  }
  delay(100);
}
