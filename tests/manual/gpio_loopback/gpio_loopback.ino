// GPIO verified through a real wire, not through the pin's own input path.
//
// core_api checks digitalWrite/digitalRead by reading an output pin back: on
// CH32 an output still feeds the input register, so that works without wiring
// - but it only proves the register round-trips, not that the pad drives
// anything. It also cannot see a pull-up, cannot cross ports, and cannot
// measure a PWM waveform.
//
// One jumper between LOOPBACK_OUT and LOOPBACK_IN covers all four.
//
// ---------------------------------------------------------------------------
// EDIT THESE to match your board and the wire you fitted.
//
// Pick two pads on DIFFERENT ports that nothing else on the board drives, and
// never the SWD pads (PA13/PA14, or PC18/PC19 on X033/X035) - driving those
// kills the debug connection mid-run.
//
//   CH32X035 (LQFP48): PA0 and PB0
//   CH32V003 (TSSOP20): PA1 and PC0
//
// They live here rather than in a -D because the pytest plugin has no way to
// pass build properties, and a manual test is edited by hand anyway.
#define LOOPBACK_OUT PA0
#define LOOPBACK_IN  PB0
// ---------------------------------------------------------------------------

static int failures = 0;

static void check(const char *name, bool ok, long detail = 0)
{
  Serial.print(name);
  if (ok) {
    Serial.println(" PASS");
  } else {
    Serial.print(" FAIL ");
    Serial.println(detail);
    failures++;
  }
}

static volatile int edges = 0;
static void on_edge() { edges++; }

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("gpio_loopback begin");
  // Printed as numbers: the pad names are macros, and the port-encoded value
  // is what every failure below is really about.
  Serial.print("out=");
  Serial.print(LOOPBACK_OUT);
  Serial.print(" in=");
  Serial.println(LOOPBACK_IN);

  check("pins_differ", LOOPBACK_OUT != LOOPBACK_IN);
  check("pins_valid", digitalPinIsValid(LOOPBACK_OUT) &&
                      digitalPinIsValid(LOOPBACK_IN));

  // --- the wire carries a level ---
  pinMode(LOOPBACK_OUT, OUTPUT);
  pinMode(LOOPBACK_IN, INPUT);
  digitalWrite(LOOPBACK_OUT, HIGH);
  delayMicroseconds(50);
  bool high = digitalRead(LOOPBACK_IN) == HIGH;
  digitalWrite(LOOPBACK_OUT, LOW);
  delayMicroseconds(50);
  bool low = digitalRead(LOOPBACK_IN) == LOW;
  // Both halves matter: a floating input often reads HIGH, so "high" alone
  // would pass with no jumper at all.
  check("level_through_wire", high && low, (high ? 2 : 0) + (low ? 1 : 0));

  // --- the input's pull-up actually pulls ---
  pinMode(LOOPBACK_OUT, INPUT);       // let go of the line
  pinMode(LOOPBACK_IN, INPUT_PULLUP);
  delayMicroseconds(200);
  bool pulled_up = digitalRead(LOOPBACK_IN) == HIGH;
  pinMode(LOOPBACK_IN, INPUT_PULLDOWN);
  delayMicroseconds(200);
  bool pulled_down = digitalRead(LOOPBACK_IN) == LOW;
  check("pullup", pulled_up);
  check("pulldown", pulled_down);

  // --- an edge on another port reaches EXTI ---
  pinMode(LOOPBACK_OUT, OUTPUT);
  pinMode(LOOPBACK_IN, INPUT);
  digitalWrite(LOOPBACK_OUT, LOW);
  delayMicroseconds(50);
  edges = 0;
  attachInterrupt(digitalPinToInterrupt(LOOPBACK_IN), on_edge, RISING);
  digitalWrite(LOOPBACK_OUT, HIGH);
  delay(2);
  int after_rise = edges;
  digitalWrite(LOOPBACK_OUT, LOW);
  delay(2);
  // RISING must not fire on the falling edge.
  check("exti_cross_port", after_rise == 1 && edges == 1, edges);
  detachInterrupt(digitalPinToInterrupt(LOOPBACK_IN));

  // --- pulseIn measures a PWM output ---
  // analogWrite is 1 kHz, so a 25% duty high pulse is about 250 us. The window
  // is wide because the timer prescaler is derived from F_CPU and rounds.
  pinMode(LOOPBACK_IN, INPUT);
  analogWrite(LOOPBACK_OUT, 64);
  delay(5);
  unsigned long width = pulseIn(LOOPBACK_IN, HIGH, 20000);
  check("pwm_duty_25pct", width > 150 && width < 400, (long)width);

  analogWrite(LOOPBACK_OUT, 191);
  delay(5);
  unsigned long wide = pulseIn(LOOPBACK_IN, HIGH, 20000);
  check("pwm_duty_75pct", wide > 550 && wide < 950, (long)wide);
  check("pwm_duty_ordered", wide > width, (long)(wide - width));

  digitalWrite(LOOPBACK_OUT, LOW);
  Serial.print("gpio_loopback done failures=");
  Serial.println(failures);
}

void loop()
{
}
