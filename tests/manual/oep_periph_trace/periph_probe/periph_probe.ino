// Peripheral outputs on request, observed by the OEP probe's fixture.capture (worklist P3 rows 4/5/7).
//   PWM <duty>            analogWrite(PA1, duty)          -> "PWM duty=<d>"
//   TONE <hz>             tone(PA1, hz)                    -> "TONE hz=<hz>"
//   NOTONE                noTone(PA1); PA1 low
//   TOGGLE <us> <count>   square wave on PA1 with delayMicroseconds(us) half periods -> "TOGGLE done"
//   MILLIS <ms> <count>   toggle PA1 every <ms> ms using millis()                    -> "MILLIS done"
//   SPI <hz> <mode> <hex> SPI1 (PA5 SCK, PA7 MOSI, PA6 MISO), CS = PA4 driven as GPIO -> "SPI got=<hex>"
// Fixture (E143): PA1 -> P4 GPIO47, PA4 -> 53, PA5 -> 4, PA6 -> 11, PA7 -> 5.
#define TC_CMD_MAX 192   // a 64-byte SPI payload is 128 hex characters
#include "testcmd.h"
#include <SPI.h>

#if defined(OEP_TARGET_V003)   // UIAPduino jig: SPI1 PC5/PC6/PC7, CS on PC3 (-> ESP32 GPIO17), PWM PA1 (-> 25)
static const uint8_t PWM_PIN = PA1, CS_PIN = PC3;
#else
static const uint8_t PWM_PIN = PA1, CS_PIN = PA4;
#endif
static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void setup() { tc_begin("periph_probe"); pinMode(PWM_PIN, OUTPUT); digitalWrite(PWM_PIN, LOW); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned long a = 0, b = 0; char arg[136] = {0};
  const int n = sscanf(cmd, "%7s %lu %lu %135s", verb, &a, &b, arg);
  if (n < 1) return;
  if (!strcmp(verb, "PWM")) { analogWrite(PWM_PIN, (int)a); Serial.print("PWM duty="); Serial.println(a); }
  else if (!strcmp(verb, "TONE")) { tone(PWM_PIN, (unsigned)a); Serial.print("TONE hz="); Serial.println(a); }
  else if (!strcmp(verb, "NOTONE")) { noTone(PWM_PIN); pinMode(PWM_PIN, OUTPUT); digitalWrite(PWM_PIN, LOW); Serial.println("NOTONE"); }
  else if (!strcmp(verb, "TOGGLE")) {
    pinMode(PWM_PIN, OUTPUT);
    for (unsigned long i = 0; i < b; ++i) { digitalWrite(PWM_PIN, HIGH); delayMicroseconds(a); digitalWrite(PWM_PIN, LOW); delayMicroseconds(a); }
    Serial.println("TOGGLE done");
  } else if (!strcmp(verb, "TOGGLE0")) {
    pinMode(PWM_PIN, OUTPUT);
    for (unsigned long i = 0; i < a; ++i) { digitalWrite(PWM_PIN, HIGH); digitalWrite(PWM_PIN, LOW); }
    Serial.println("TOGGLE0 done");
  } else if (!strcmp(verb, "MILLIS")) {
    pinMode(PWM_PIN, OUTPUT); bool level = false; unsigned long next = millis() + a; unsigned long toggles = 0;
    const unsigned long t0 = millis();
    for (unsigned long i = 0; i < b; ++i) { while ((long)(millis() - next) < 0) {} next += a; level = !level; digitalWrite(PWM_PIN, level ? HIGH : LOW); ++toggles; }
    const unsigned long dt = millis() - t0;
    digitalWrite(PWM_PIN, LOW); Serial.print("MILLIS done toggles="); Serial.print(toggles); Serial.print(" ms="); Serial.println(dt);
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
    Serial.print("SPI got=");
    for (size_t i = 0; i < count; ++i) { if (buf[i] < 16) Serial.print('0'); Serial.print(buf[i], HEX); }
    Serial.println();
  } else { Serial.print("ERR verb "); Serial.println(verb); }
}
