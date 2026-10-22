/* Wiring, operation and expected output: README.md / README.ja.md. */
/* NotifyBits - several kinds of event in one object: each ISR raises its
 * own bit, loop() asks for any of them or for all of them at once.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_A = PA1;   /* two push buttons to GND: change to your pins */
static const int BUTTON_B = PA2;
static const uint32_t BIT_A = 1u << 0;
static const uint32_t BIT_B = 1u << 1;

static CH32RVSyncKit::Notify<> events;

static void onA() { events.setBits(BIT_A); }
static void onB() { events.setBits(BIT_B); }

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_A), onA, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_B), onB, FALLING);
  Serial.println("NotifyBits ready");
}

void loop()
{
  /* Both pressed, in any order, since both were last seen: waitAll=true takes
   * them only when both are up; clearOnExit=true clears them in the same step. */
  if (events.tryWaitBits(BIT_A | BIT_B, true, true))
  {
    Serial.println("BOTH");
  }
  /* Look without taking (clearOnExit=false), for a status line. */
  const uint32_t up = events.tryWaitBits(BIT_A | BIT_B, false);
  Serial.print("WAITING");
  Serial.print((up & BIT_A) ? "" : " A");
  Serial.println((up & BIT_B) ? "" : " B");
  delay(500);
}
