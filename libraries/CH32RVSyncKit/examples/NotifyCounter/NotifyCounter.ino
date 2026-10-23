// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::Notify<> presses;

static void onPress()
{
  presses.notify();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("NotifyCounter ready");
}

void loop()
{
  // en: Take and reset the pending count together. / ja: 保留中の回数を一度に取り出してリセットする。
  const uint32_t n = presses.tryTakeAll();
  if (n)
  {
    Serial.print("PRESSES since_last=");
    Serial.println(n);
  }
  delay(500);
}
