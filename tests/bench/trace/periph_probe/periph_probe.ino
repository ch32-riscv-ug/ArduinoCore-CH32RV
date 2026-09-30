// Peripheral outputs on request, observed by the OEP probe's fixture.capture (tests/bench/trace/periph_probe).
//   PINS <pwm> <cs>       which pins to drive: the core's pin numbers, from the bench file -> "PINS ok"
//   PWM <duty>            analogWrite(pwm, duty)                                     -> "PWM duty=<d>"
//   TONE <hz>             tone(pwm, hz)                                              -> "TONE hz=<hz>"
//   NOTONE                noTone(pwm); pwm low                                       -> "NOTONE"
//   TOGGLE <us> <count>   square wave on pwm with delayMicroseconds(us) half periods -> "TOGGLE done"
//   TOGGLE0 <count>       digitalWrite high / low pairs, no delay                    -> "TOGGLE0 done"
//   MILLIS <ms> <count>   toggle pwm every <ms> ms using millis()                    -> "MILLIS done"
//   SPI <hz> <mode> <hex> SPI (the variant's default route), cs driven as GPIO       -> "SPI got=<hex>"
// No pin is baked in: the host reads the bench file and sends PINS first. Defaults are PA1 / PA4.
#define TC_CMD_MAX 192   // a 64-byte SPI payload is 128 hex characters
#include "testcmd.h"
#include <SPI.h>

static uint8_t PWM_PIN = PA1, CS_PIN = PA4;
static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void setup() { tc_begin("periph_probe"); pinMode(PWM_PIN, OUTPUT); digitalWrite(PWM_PIN, LOW); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned long a = 0, b = 0; char arg[136] = {0};
  const int n = sscanf(cmd, "%7s %lu %lu %135s", verb, &a, &b, arg);
  if (n < 1) return;
  if (!strcmp(verb, "PINS")) {
    noTone(PWM_PIN); pinMode(PWM_PIN, INPUT);
    PWM_PIN = (uint8_t)a; CS_PIN = (uint8_t)b;
    pinMode(PWM_PIN, OUTPUT); digitalWrite(PWM_PIN, LOW);
    Console.println("PINS ok");
  } else if (!strcmp(verb, "PWM")) { analogWrite(PWM_PIN, (int)a); Console.print("PWM duty="); Console.println(a); }
  else if (!strcmp(verb, "TONE")) { tone(PWM_PIN, (unsigned)a); Console.print("TONE hz="); Console.println(a); }
  else if (!strcmp(verb, "NOTONE")) { noTone(PWM_PIN); pinMode(PWM_PIN, OUTPUT); digitalWrite(PWM_PIN, LOW); Console.println("NOTONE"); }
  else if (!strcmp(verb, "TOGGLE")) {
    pinMode(PWM_PIN, OUTPUT);
    for (unsigned long i = 0; i < b; ++i) { digitalWrite(PWM_PIN, HIGH); delayMicroseconds(a); digitalWrite(PWM_PIN, LOW); delayMicroseconds(a); }
    Console.println("TOGGLE done");
  } else if (!strcmp(verb, "TOGGLE0")) {
    pinMode(PWM_PIN, OUTPUT);
    for (unsigned long i = 0; i < a; ++i) { digitalWrite(PWM_PIN, HIGH); digitalWrite(PWM_PIN, LOW); }
    Console.println("TOGGLE0 done");
  } else if (!strcmp(verb, "MILLIS")) {
    pinMode(PWM_PIN, OUTPUT); bool level = false; unsigned long next = millis() + a; unsigned long toggles = 0;
    const unsigned long t0 = millis();
    for (unsigned long i = 0; i < b; ++i) { while ((long)(millis() - next) < 0) {} next += a; level = !level; digitalWrite(PWM_PIN, level ? HIGH : LOW); ++toggles; }
    const unsigned long dt = millis() - t0;
    digitalWrite(PWM_PIN, LOW); Console.print("MILLIS done toggles="); Console.print(toggles); Console.print(" ms="); Console.println(dt);
  } else if (!strcmp(verb, "SPI")) {
    uint8_t buf[64]; size_t count = 0;
    for (const char *p = arg; p[0] && p[1] && count < sizeof buf; p += 2) buf[count++] = (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));
    static bool started = false;
    if (!started) { pinMode(CS_PIN, OUTPUT); digitalWrite(CS_PIN, HIGH); SPI.begin(); started = true; }
    const uint8_t modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    SPI.beginTransaction(SPISettings(a, MSBFIRST, modes[b & 3]));
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(buf, count);
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
    Console.print("SPI got=");
    for (size_t i = 0; i < count; ++i) { if (buf[i] < 16) Console.print('0'); Console.print(buf[i], HEX); }
    Console.println();
  } else { Console.print("ERR verb "); Console.println(verb); }
}
