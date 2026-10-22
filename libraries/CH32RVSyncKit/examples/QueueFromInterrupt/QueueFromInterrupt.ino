/* Wiring, operation and expected output: README.md / README.ja.md. */
/* QueueFromInterrupt - an interrupt hands data to loop() through a Queue.
 * The ISR only stores the time of the press and returns; loop() prints.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

/* 8 values, shared with an ISR (IsrSafe, the default). */
static CH32RVSyncKit::Queue<uint32_t, 8> presses;
static volatile uint32_t dropped = 0;   /* written by the ISR only */

static void onPress()
{
  /* Never wait in an ISR: when the queue is full, count it and return. */
  if (!presses.trySend(millis()))
  {
    dropped = dropped + 1;
  }
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("QueueFromInterrupt ready");
}

void loop()
{
  /* Take everything that arrived, then go on with the rest of loop(). */
  uint32_t t;
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
    dropped = 0;   /* one aligned 32-bit store: the ISR cannot see half of it */
  }
  delay(200);   /* a slow loop: presses wait in the queue meanwhile */
}
