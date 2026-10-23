// en: Queue complete variable-length messages from an interrupt to loop().
// ja: 長さの異なるメッセージを、割り込みからloop()へ丸ごと渡す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static CH32RVSyncKit::MessageBuffer<48> messages;
static volatile uint32_t refused = 0;

static void onPress()
{
  static bool longOne = false;
  static const char shortMsg[] = "hi";
  static const char longMsg[] = "a longer message";
  // en: Store each message as a whole; no partial prefix is queued if the buffer cannot fit it.
  // ja: メッセージは丸ごと保存する。容量不足なら途中までの断片も入れない。
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
  // en: The pull-up keeps an open button High; pressing it to GND makes the input Low.
  // ja: プルアップでボタン開放時をHighに保ち、GNDへ押すとLowになる。
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // en: Register an interrupt handler for the falling edge; the handler can interrupt loop().
  // ja: HighからLowへの変化に割り込み関数を登録する。この関数はloop()の途中にも動く。
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
    // en: The payload is raw bytes; append a zero terminator before using the text-printing API.
    // ja: 受信内容はバイト列なので、文字列として表示する前に終端の0を付ける。
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
