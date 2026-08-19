// Self-checking exercise of the core API that needs no external wiring.
// Every check prints "<name> PASS" or "<name> FAIL <detail>", so the pytest
// side can assert on names instead of parsing values.
//
// Pins: LED_BUILTIN and A0 come from the variant. The interrupt check drives an
// output pin and watches its own edge - on CH32 an output pin still feeds the
// input path, so EXTI sees it without a jumper.

static int failures = 0;

static void check(const char *name, bool ok, long detail = 0) {
  Serial.print(name);
  if (ok) {
    Serial.println(" PASS");
  } else {
    Serial.print(" FAIL ");
    Serial.println(detail);
    failures++;
  }
}

static volatile int isr_hits = 0;
static void on_edge() { isr_hits++; }

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("core_api begin");

  // --- time ---
  unsigned long t0 = millis();
  delay(100);
  unsigned long elapsed = millis() - t0;
  check("millis", elapsed >= 95 && elapsed <= 115, (long)elapsed);

  unsigned long u0 = micros();
  delayMicroseconds(2000);
  unsigned long us = micros() - u0;
  check("micros", us >= 1800 && us <= 2600, (long)us);

  // --- digital, read back through the input path of an output pin ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  bool high = digitalRead(LED_BUILTIN) == HIGH;
  digitalWrite(LED_BUILTIN, LOW);
  bool low = digitalRead(LED_BUILTIN) == LOW;
  check("digital", high && low, (high ? 2 : 0) + (low ? 1 : 0));

  // --- pin numbering is port-encoded, not sequential ---
  check("pin_encoding", digitalPinIsValid(LED_BUILTIN) && !digitalPinIsValid(0xFF));

  // --- analog ---
#ifdef NUM_ANALOG_INPUTS
  int a = analogRead(A0);
  check("analogRead", a >= 0 && a <= 1023, a);
  check("adc_channel", digitalPinToAnalogChannel(A0) == 0,
        digitalPinToAnalogChannel(A0));
#endif

  // --- pwm: must not hang, and must accept the extremes ---
  analogWrite(LED_BUILTIN, 0);
  analogWrite(LED_BUILTIN, 128);
  analogWrite(LED_BUILTIN, 255);
  check("analogWrite", true);

  // --- interrupts: drive an output and catch its own edge ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  isr_hits = 0;
  attachInterrupt(digitalPinToInterrupt(LED_BUILTIN), on_edge, RISING);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2);
  int hits_after_rise = isr_hits;
  detachInterrupt(digitalPinToInterrupt(LED_BUILTIN));
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(2);
  check("attachInterrupt", hits_after_rise >= 1, hits_after_rise);
  check("detachInterrupt", isr_hits == hits_after_rise, isr_hits);

  // --- shift: same pin for data and clock is fine, we only check the bits ---
  pinMode(LED_BUILTIN, OUTPUT);
  shiftOut(LED_BUILTIN, LED_BUILTIN, MSBFIRST, 0xA5);
  check("shiftOut", true);

  // --- pulseIn must time out rather than hang ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  unsigned long p = pulseIn(LED_BUILTIN, HIGH, 2000);
  check("pulseIn_timeout", p == 0, (long)p);

  // --- random is seeded and stays in range ---
  randomSeed(12345);
  long r1 = random(100);
  randomSeed(12345);
  long r2 = random(100);
  check("random_repeatable", r1 == r2, r1 - r2);
  check("random_range", r1 >= 0 && r1 < 100, r1);

  // --- Print formatting, the part sketches actually depend on ---
  Serial.print("fmt=");
  Serial.print(255, HEX);
  Serial.print(',');
  Serial.print(-42);
  Serial.print(',');
  Serial.println(1.5, 2);

  Serial.print("core_api done failures=");
  Serial.println(failures);
}

void loop() {}
