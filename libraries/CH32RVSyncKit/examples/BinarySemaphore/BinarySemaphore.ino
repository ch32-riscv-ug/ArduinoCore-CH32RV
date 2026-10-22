/* Wiring, operation and expected output: README.md / README.ja.md. */
/* BinarySemaphore - "it happened" once: the ISR gives, loop() takes. A
 * second give before the take is refused, so loop() learns it was slow.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

static CH32RVSyncKit::BinarySemaphore<> pressed;
static volatile uint32_t missed = 0;

static void onPress()
{
  if (!pressed.give())
  {
    missed = missed + 1;   /* given already and not taken yet */
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("BinarySemaphore ready");
}

void loop()
{
  if (pressed.tryTake())
  {
    Serial.println("PRESSED");
  }
  if (missed)
  {
    Serial.print("MISSED ");
    Serial.println(missed);
    missed = 0;
  }
  delay(1000);   /* slow on purpose: press twice quickly */
}
