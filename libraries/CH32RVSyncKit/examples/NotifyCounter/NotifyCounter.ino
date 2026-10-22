/* Wiring, operation and expected output: README.md / README.ja.md. */
/* NotifyCounter - count events without their data: the ISR calls notify(),
 * loop() takes them. A counting semaphore that nobody waits on.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

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
  /* How many since the last look, taken in one step. */
  const uint32_t n = presses.tryTakeAll();
  if (n)
  {
    Serial.print("PRESSES since_last=");
    Serial.println(n);
  }
  delay(500);
}
