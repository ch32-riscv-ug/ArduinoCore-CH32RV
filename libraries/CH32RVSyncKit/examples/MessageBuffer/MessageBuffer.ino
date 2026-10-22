/* Wiring, operation and expected output: README.md / README.ja.md. */
/* MessageBuffer - messages of different lengths, each kept whole: trySend()
 * stores all of a message or none of it, tryReceive() returns one whole
 * message. Each press queues a short and a long message alternately.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

/* 48 bytes of storage; each message costs its length + 2. */
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
    refused = refused + 1;   /* no room for the whole message: nothing stored */
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
    const size_t n = messages.tryReceive(text, sizeof text - 1);
    if (n == 0 && messages.nextLength() > sizeof text - 1)
    {
      Serial.println("TOO_LONG for this buffer");   /* not here, but this is how it shows */
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
  delay(2000);   /* slow on purpose: press quickly to fill it */
}
