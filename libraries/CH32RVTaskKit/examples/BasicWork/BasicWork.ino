/* Wiring, operation and expected output: README.md / README.ja.md. */
/* BasicWork - two pieces of work that run only when their trigger says so: an
 * LED on a Period, and a line reader whenever the UART has bytes. loop() is
 * one call to runOnce(), the one place either of them runs.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVTaskKit.h>

static const int LED_PIN = PA1;   /* an LED (with its resistor): change to your pin */

static CH32RVTaskKit::Executor app;
static CH32RVSyncKit::Period every250(250);
static bool ledOn = false;

static void blinkStep()
{
  ledOn = !ledOn;
  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}

static void readStep()
{
  /* Take what is there now, then return: the rest waits for the next round. */
  while (Serial.available())
  {
    const int c = Serial.read();
    if (c == '\n')
    {
      Serial.println("LINE");
    }
  }
}

static bool uartHasBytes()
{
  return Serial.available() > 0;
}

static CH32RVTaskKit::Work blink(blinkStep);
static CH32RVTaskKit::Work reader(readStep);

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  pinMode(LED_PIN, OUTPUT);
  app.attach(blink);                          /* runs once in the first round, then on its trigger */
  app.attach(reader);
  blink.wakeEvery(every250);
  reader.wakeWhen(uartHasBytes);
  Serial.println("BasicWork ready");
}

void loop()
{
  app.runOnce();
}
