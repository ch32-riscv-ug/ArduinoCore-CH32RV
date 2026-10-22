/* Wiring, operation and expected output: README.md / README.ja.md. */
/* DeadlinePeriod - two things on their own schedules without delay(): an
 * LED that blinks on a Period, and a Deadline that stops waiting for a line
 * on the UART. loop() never stops; each part only answers "is it time?".
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int LED_PIN = PA1;   /* an LED (with its resistor): change to your pin */

static CH32RVSyncKit::Period blink(250);
static CH32RVSyncKit::Deadline answer;
static bool waiting = false;
static bool ledOn = false;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(LED_PIN, OUTPUT);
  Serial.println("DeadlinePeriod ready");
}

void loop()
{
  if (blink.due())
  {
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  }
  if (blink.missed())
  {
    /* loop() was late by whole periods: they were skipped, not run in a burst. */
    Serial.print("LATE skipped=");
    Serial.println(blink.missed());
    blink.clearMissed();
  }

  if (!waiting)
  {
    Serial.println("TYPE something within 5 s");
    answer.arm(5000);
    waiting = true;
  }
  else if (Serial.available())
  {
    while (Serial.available())
    {
      Serial.read();
    }
    Serial.print("THANKS spare_ms=");
    Serial.println(answer.remaining());
    waiting = false;
  }
  else if (answer.expired())
  {
    Serial.println("TOO_SLOW");
    waiting = false;
  }
}
