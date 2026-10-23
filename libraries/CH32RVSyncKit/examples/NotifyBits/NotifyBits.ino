// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_A = PA1;
static const int BUTTON_B = PA2;
static const uint32_t BIT_A = 1u << 0;
static const uint32_t BIT_B = 1u << 1;

static CH32RVSyncKit::Notify<> events;

static void onA() { events.setBits(BIT_A); }
static void onB() { events.setBits(BIT_B); }

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_A), onA, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_B), onB, FALLING);
  Serial.println("NotifyBits ready");
}

void loop()
{
  // en: Require both event bits, then clear them together; this is not simultaneous button state.
  // ja: 両イベントがそろったらまとめて消す。ボタンの同時押し状態の判定ではない。
  if (events.tryWaitBits(BIT_A | BIT_B, true, true))
  {
    Serial.println("BOTH");
  }

  // en: Inspect without consuming pending bits (clearOnExit=false). / ja: clearOnExit=falseで、保留中のbitを消さずに見る。
  const uint32_t up = events.tryWaitBits(BIT_A | BIT_B, false);
  Serial.print("WAITING");
  Serial.print((up & BIT_A) ? "" : " A");
  Serial.println((up & BIT_B) ? "" : " B");
  delay(500);
}
