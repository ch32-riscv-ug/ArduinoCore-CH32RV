// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::Queue<uint32_t, 8> presses;
static volatile uint32_t dropped = 0;

static void onPress()
{
  // en: Never wait in the ISR; count a full-queue refusal and return. / ja: ISRでは待たず、Queue満杯の拒否を数えて戻る。
  if (!presses.trySend(millis()))
  {
    dropped = dropped + 1;
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("QueueFromInterrupt ready");
}

void loop()
{
  uint32_t t;
  // en: Drain queued values in arrival order outside the ISR. / ja: ISRの外で、到着順にQueueを読み出す。
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
  delay(200);
}
