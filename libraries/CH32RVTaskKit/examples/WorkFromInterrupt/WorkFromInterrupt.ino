/* Wiring, operation and expected output: README.md / README.ja.md. */
/* WorkFromInterrupt - an interrupt does not do the work, it asks for it: the
 * ISR stores the press time in a Queue and posts the Work; loop()'s runOnce()
 * runs it. Several presses before the run are one run that drains them all.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVTaskKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Queue<uint32_t, 8> presses;

static void handlePresses()
{
  uint32_t t;
  uint32_t n = 0;
  while (presses.tryReceive(t))
  {
    n++;
    Serial.print("PRESS ms=");
    Serial.println(t);
  }
  Serial.print("RUN handled=");
  Serial.println(n);
}

static CH32RVTaskKit::Work handler(handlePresses);

static void onPress()
{
  presses.trySend(millis());   /* the data */
  handler.post();              /* "please run": from an ISR too */
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  app.attach(handler);
  Serial.println("WorkFromInterrupt ready");
}

void loop()
{
  app.runOnce();
  delay(300);   /* a slow loop: presses meanwhile become one run */
}
