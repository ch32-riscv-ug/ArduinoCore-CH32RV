// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::MessageBuffer<48> messages;
static volatile uint32_t refused = 0;

static void onPress()
{
  static bool longOne = false;
  static const char shortMsg[] = "hi";
  static const char longMsg[] = "a longer message";
  const bool ok = longOne ? messages.trySend(longMsg, sizeof longMsg - 1)
                          : messages.trySend(shortMsg, sizeof shortMsg - 1);
  if (!ok)
  {
    refused = refused + 1;
  }
  longOne = !longOne;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("MessageBuffer ready");
}

void loop()
{
  char text[32];
  while (messages.count())
  {
    // en: Leave one byte for a text terminator; an oversized message remains queued.
    // ja: 文字列終端の1 byte分を残す。入りきらないメッセージはQueueに残る。
    const size_t n = messages.tryReceive(text, sizeof text - 1);
    if (n == 0 && messages.nextLength() > sizeof text - 1)
    {
      Serial.println("TOO_LONG for this buffer");
      break;
    }
    text[n] = 0;
    Serial.print("MESSAGE ");
    Serial.println(text);
  }
  if (refused)
  {
    Serial.print("REFUSED ");
    Serial.println(refused);
    refused = 0;
  }
  delay(2000);
}
