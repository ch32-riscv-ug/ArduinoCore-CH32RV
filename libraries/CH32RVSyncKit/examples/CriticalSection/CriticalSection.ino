/* Wiring, operation and expected output: README.md / README.ja.md. */
/* CriticalSection - read several variables an ISR writes as one consistent
 * snapshot. Without it, an interrupt between two reads can pair the count
 * of one press with the time of another.
 *
 * A CriticalSection masks this core's interrupts for its scope and puts
 * them back as they were (it nests). Keep it to a few lines: every
 * interrupt - USB, USB PD, the UARTs - waits while it is held. CH32V003 has
 * no atomic instructions, so masking is how a multi-word value is shared
 * with an ISR there. docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;   /* a push button to GND: change to your pin */

/* Written by the ISR; volatile, so every read goes to memory. */
static volatile uint32_t pressCount = 0;
static volatile uint32_t lastPressMs = 0;
static volatile uint64_t sumOfMicros = 0;   /* 64 bits: two words on RV32 */

static void onPress()
{
  /* Inside an ISR this core's interrupts are already off: no guard needed. */
  pressCount = pressCount + 1;
  lastPressMs = millis();
  sumOfMicros = sumOfMicros + micros();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("CriticalSection ready");
}

void loop()
{
  uint32_t count, last;
  uint64_t sum;
  {
    CH32RVSyncKit::CriticalSection cs;   /* interrupts off up to the closing brace */
    count = pressCount;                  /* copy, nothing else */
    last = lastPressMs;
    sum = sumOfMicros;
  }
  /* Printing is slow: do it from the copies, with interrupts on. */
  Serial.print("PRESSES count=");
  Serial.print(count);
  Serial.print(" last_ms=");
  Serial.print(last);
  Serial.print(" sum_us_low=");
  Serial.println((unsigned long)(sum & 0xFFFFFFFFu));
  delay(1000);
}
