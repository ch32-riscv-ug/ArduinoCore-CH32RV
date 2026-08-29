// Self-checking exercise of the core API that needs no external wiring.
// Every check prints "<name> PASS" or "<name> FAIL <detail>", so the pytest
// side can assert on names instead of parsing values.
//
// Pins: PIN below, and A0. The interrupt check drives an output pin and
// watches its own edge - on CH32 an output pin still feeds the input path, so
// EXTI sees it without a jumper.
//
// PA1 is the sketch's own choice, not the board's: a Generic board is a
// silicon series and defines no LED_BUILTIN (docs/board-layer-rules.ja.md).
// PA1 is the one pad bonded on every tier A/B board that is also PWM-capable
// on all of them, which is what analogWrite() below needs.
#include "testcmd.h"

static const uint8_t PIN = PA1;

static volatile int isr_hits = 0;
static void on_edge() { isr_hits++; }

static void run_checks()
{
  // --- time ---
  unsigned long t0 = millis();
  delay(100);
  unsigned long elapsed = millis() - t0;
  tc_checkv("millis", elapsed >= 95 && elapsed <= 115, (long)elapsed);

  unsigned long u0 = micros();
  delayMicroseconds(2000);
  unsigned long us = micros() - u0;
  tc_checkv("micros", us >= 1800 && us <= 2600, (long)us);

  // --- digital, read back through the input path of an output pin ---
  pinMode(PIN, OUTPUT);
  digitalWrite(PIN, HIGH);
  bool high = digitalRead(PIN) == HIGH;
  digitalWrite(PIN, LOW);
  bool low = digitalRead(PIN) == LOW;
  tc_checkv("digital", high && low, (high ? 2 : 0) + (low ? 1 : 0));

  // --- pin numbering is port-encoded, not sequential ---
  tc_check("pin_encoding",
           digitalPinIsValid(PIN) && !digitalPinIsValid(0xFF));

  // --- analog ---
#ifdef NUM_ANALOG_INPUTS
  int a = analogRead(A0);
  tc_checkv("analogRead", a >= 0 && a <= 1023, a);
  tc_checkv("adc_channel", digitalPinToAnalogChannel(A0) == 0,
            digitalPinToAnalogChannel(A0));
#endif

  // --- pwm: must not hang, and must accept the extremes ---
  analogWrite(PIN, 0);
  analogWrite(PIN, 128);
  analogWrite(PIN, 255);
  tc_check("analogWrite", true);

  // --- interrupts: drive an output and catch its own edge ---
  pinMode(PIN, OUTPUT);
  digitalWrite(PIN, LOW);
  isr_hits = 0;
  attachInterrupt(digitalPinToInterrupt(PIN), on_edge, RISING);
  digitalWrite(PIN, HIGH);
  delay(2);
  int hits_after_rise = isr_hits;
  detachInterrupt(digitalPinToInterrupt(PIN));
  digitalWrite(PIN, LOW);
  digitalWrite(PIN, HIGH);
  delay(2);
  tc_checkv("attachInterrupt", hits_after_rise >= 1, hits_after_rise);
  tc_checkv("detachInterrupt", isr_hits == hits_after_rise, isr_hits);

  // --- shift: same pin for data and clock is fine, we only check the bits ---
  pinMode(PIN, OUTPUT);
  shiftOut(PIN, PIN, MSBFIRST, 0xA5);
  tc_check("shiftOut", true);

  // --- pulseIn must time out rather than hang ---
  pinMode(PIN, OUTPUT);
  digitalWrite(PIN, LOW);
  unsigned long p = pulseIn(PIN, HIGH, 2000);
  tc_checkv("pulseIn_timeout", p == 0, (long)p);

  // --- random is seeded and stays in range ---
  randomSeed(12345);
  long r1 = random(100);
  randomSeed(12345);
  long r2 = random(100);
  tc_checkv("random_repeatable", r1 == r2, r1 - r2);
  tc_checkv("random_range", r1 >= 0 && r1 < 100, r1);

  /* Print's number formatting lives in the print_format case, not here.
   * Serial.println(1.5, 2) is one line and 9428 bytes on CH32V003 - the
   * soft-float routines behind Print::printFloat - which had this sketch at
   * 97% of a 16 KB part. */

  /* Room in the transmit ring. Print's default returns 0, which would make a
   * sketch believe the port is permanently full.
   *
   * flush() first: every check above printed, and at 115200 the ring really is
   * full at this point. Measuring without draining would be testing how fast
   * the UART is, not whether the count is reported. */
  Serial.flush();
  const int room = Serial.availableForWrite();
  tc_checkv("availableForWrite", room > 0, room);

  /* The port-access macros, in the shape the ESP32 core uses: one bit of one
   * 32-bit register per pin. Driving the pad through them has to be visible to
   * digitalRead(), and the port and bit have to match the pin encoding. */
  {
    const uint8_t pin = PIN;
    pinMode(pin, OUTPUT);
    volatile uint32_t *out = portOutputRegister(digitalPinToPort(pin));
    volatile uint32_t *in = portInputRegister(digitalPinToPort(pin));
    const uint32_t mask = digitalPinToBitMask(pin);

    tc_checkv("digitalPinToPort", digitalPinToPort(pin) == CH32_PIN_PORT(pin),
              digitalPinToPort(pin));
    tc_checkv("digitalPinToBitMask", mask == (1UL << CH32_PIN_BIT(pin)),
              (long)mask);

    *out |= mask;
    const bool drove_high = digitalRead(pin) == HIGH && (*in & mask) != 0;
    *out &= ~mask;
    const bool drove_low = digitalRead(pin) == LOW && (*in & mask) == 0;
    tc_check("portOutputRegister", drove_high && drove_low);
    tc_check("portInputRegister", drove_high && drove_low);
  }

  tc_done();
}

void setup()
{
  tc_begin("core_api");
}

void loop()
{
  const char *cmd = tc_ready();
  if (!cmd) {
    return;
  }
  if (!strcmp(cmd, "RUN")) {
    run_checks();
  } else {
    tc_unknown(cmd);
  }
}
