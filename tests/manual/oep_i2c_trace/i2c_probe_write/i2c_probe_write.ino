// I2C master transactions on request, for the OEP capture trace (worklist B).
// The host drives it over the console (USART4 on the P4 fixture) and watches the
// bus with fixture.capture at the same time, so the sketch only reports what the
// core's Wire returned.
//
//   WRITE <route> <hz> <addr_hex> <bytes_hex>   -> "WRITE rc=<n> route=<r> hz=<hz> n=<bytes>"
//   READ  <route> <hz> <addr_hex> <count>       -> "READ got=<n> data=<hex>"
//
// Route 2 on CH32X035 is PC16 (SCL) / PC17 (SDA), wired to the P4 GPIO52 / GPIO50.
#define TC_CMD_MAX 96
#include "testcmd.h"
#include <Wire.h>

static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

// BEGIN <route> re-opens the bus (Wire.begin) on purpose; WRITE/READ only set the clock,
// so a trace can separate "first transaction after begin()" from a settled bus.
static int gRoute = -1;
static bool selectRoute(uint8_t route, unsigned long hz) {
  if (gRoute != (int)route) {
    if (!Wire.setRoute(route)) { Serial.print("ERR route "); Serial.println(route); return false; }
    gRoute = route;
    Wire.begin();
  }
  Wire.setClock(hz);
  return true;
}

// Bit-banged master on the route-2 pins, for separating what the ESP32-P4 slave dislikes about the
// hardware I2C waveform: BB <hold_us> <drive> <addr_hex> <bytes_hex>. drive 0 = open-drain for both
// lines (rise time set by the weak pull-up, like Wire), 1 = push-pull highs on SDA and SCL (fast rise;
// SDA is released to INPUT_PULLUP only in the ACK slot). hold_us = SDA change after the SCL fall.
static const uint8_t BB_SCL = PC16, BB_SDA = PC17;
static void bbHigh(uint8_t pin, bool pp) { if (pp) { pinMode(pin, OUTPUT); digitalWrite(pin, HIGH); } else { pinMode(pin, OUTPUT_OPENDRAIN); digitalWrite(pin, HIGH); } }
static void bbLow(uint8_t pin, bool pp) { pinMode(pin, pp ? OUTPUT : OUTPUT_OPENDRAIN); digitalWrite(pin, LOW); }
static void bitbang(unsigned hold_us, bool pp, uint8_t addr, const uint8_t *data, size_t n) {
  bbHigh(BB_SDA, pp); bbHigh(BB_SCL, pp); delayMicroseconds(20);
  bbLow(BB_SDA, pp); delayMicroseconds(5);                       // START
  char acks[72]; size_t na = 0;
  uint8_t bytes[65]; bytes[0] = addr << 1; if (n > 64) n = 64; memcpy(bytes + 1, data, n);
  for (size_t b = 0; b < n + 1; ++b) {
    for (int i = 7; i >= 0; --i) {
      bbLow(BB_SCL, pp);
      if (hold_us) delayMicroseconds(hold_us);
      if ((bytes[b] >> i) & 1) bbHigh(BB_SDA, pp); else bbLow(BB_SDA, pp);
      delayMicroseconds(5); bbHigh(BB_SCL, pp); delayMicroseconds(5);
    }
    bbLow(BB_SCL, pp);
    if (hold_us) delayMicroseconds(hold_us);
    pinMode(BB_SDA, INPUT_PULLUP);                                  // release SDA for the ACK
    delayMicroseconds(5); bbHigh(BB_SCL, pp); delayMicroseconds(2);
    acks[na++] = digitalRead(BB_SDA) ? 'N' : 'A'; delayMicroseconds(3);
  }
  acks[na] = 0;
  bbLow(BB_SCL, pp); delayMicroseconds(2); bbLow(BB_SDA, pp); delayMicroseconds(5);   // STOP
  bbHigh(BB_SCL, pp); delayMicroseconds(5); bbHigh(BB_SDA, pp); delayMicroseconds(5);
  pinMode(BB_SDA, INPUT); pinMode(BB_SCL, INPUT);
  Serial.print("BB hold_us="); Serial.print(hold_us); Serial.print(" drive="); Serial.print(pp ? "pp" : "od");
  Serial.print(" acks="); Serial.println(acks);
}

// Fast bit-bang with sub-microsecond SDA hold: BBF <hold_ns> <addr_hex> <bytes_hex>. Both lines stay
// GPIO open-drain outputs (no pinMode in the loop); SCL 5/5 us; hold_ns is a calibrated spin after the
// SCL fall before SDA changes. The capture measures the real hold.
static inline void spinNs(uint32_t ns) {
  // ~4 cycles per iteration at 48 MHz -> 12 iterations per microsecond (calibrated against the capture)
  for (volatile uint32_t i = ns * 12u / 1000u; i; --i) { __asm__ volatile("nop"); }
}
static void bitbangFast(uint32_t hold_ns, uint8_t addr, const uint8_t *data, size_t n) {
  pinMode(BB_SDA, OUTPUT_OPENDRAIN); pinMode(BB_SCL, OUTPUT_OPENDRAIN);
  digitalWrite(BB_SDA, HIGH); digitalWrite(BB_SCL, HIGH); delayMicroseconds(20);
  digitalWrite(BB_SDA, LOW); delayMicroseconds(5);                 // START
  char acks[72]; size_t na = 0;
  uint8_t bytes[65]; bytes[0] = addr << 1; if (n > 64) n = 64; memcpy(bytes + 1, data, n);
  for (size_t b = 0; b < n + 1; ++b) {
    for (int i = 7; i >= 0; --i) {
      digitalWrite(BB_SCL, LOW); spinNs(hold_ns);
      digitalWrite(BB_SDA, (bytes[b] >> i) & 1);
      delayMicroseconds(5); digitalWrite(BB_SCL, HIGH); delayMicroseconds(5);
    }
    digitalWrite(BB_SCL, LOW); spinNs(hold_ns); digitalWrite(BB_SDA, HIGH);   // release for the ACK
    delayMicroseconds(5); digitalWrite(BB_SCL, HIGH); delayMicroseconds(2);
    acks[na++] = digitalRead(BB_SDA) ? 'N' : 'A'; delayMicroseconds(3);
  }
  acks[na] = 0;
  digitalWrite(BB_SCL, LOW); delayMicroseconds(2); digitalWrite(BB_SDA, LOW); delayMicroseconds(5);  // STOP
  digitalWrite(BB_SCL, HIGH); delayMicroseconds(5); digitalWrite(BB_SDA, HIGH); delayMicroseconds(5);
  pinMode(BB_SDA, INPUT); pinMode(BB_SCL, INPUT);
  Serial.print("BBF hold_ns="); Serial.print(hold_ns); Serial.print(" acks="); Serial.println(acks);
}

void setup() { tc_begin("i2c_probe_write"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8]; unsigned route, addr; unsigned long hz; char arg[72] = {0};
  const int n = sscanf(cmd, "%7s %u %lu %x %71s", verb, &route, &hz, &addr, arg);
  if (n >= 2 && !strcmp(verb, "BEGIN")) {
    if (!Wire.setRoute((uint8_t)route)) { Serial.print("ERR route "); Serial.println(route); return; }
    gRoute = route; Wire.begin();
    Serial.print("BEGIN route="); Serial.println(route);
    return;
  }
  if (n >= 4 && !strcmp(verb, "BBF")) {
    // BBF <hold_ns> <addr_hex> <bytes_hex>: sscanf mapped hold_ns -> route, addr -> hz (decimal!), bytes -> ... re-parse
    uint32_t hold_ns = 0; unsigned a = 0; char hexs[72] = {0};
    if (sscanf(cmd, "%*s %lu %x %71s", (unsigned long *)&hold_ns, &a, hexs) < 3) { Serial.println("ERR BBF usage"); return; }
    uint8_t bytes[64]; size_t count = 0;
    for (const char *p = hexs; p[0] && p[1] && count < sizeof bytes; p += 2) bytes[count++] = (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));
    bitbangFast(hold_ns, (uint8_t)a, bytes, count);
    return;
  }
  if (n >= 5 && !strcmp(verb, "BB")) {
    // BB <hold_us> <drive> <addr_hex> <bytes_hex>: sscanf mapped hold_us -> route, drive -> hz, addr, arg
    uint8_t bytes[64]; size_t count = 0;
    for (const char *p = arg; p[0] && p[1] && count < sizeof bytes; p += 2) bytes[count++] = (uint8_t)((hexval(p[0]) << 4) | hexval(p[1]));
    bitbang(route, hz != 0, (uint8_t)addr, bytes, count);
    return;
  }
  if (n < 5) { Serial.print("ERR usage: "); Serial.println(cmd); return; }
  if (!selectRoute((uint8_t)route, hz)) return;
  if (!strcmp(verb, "WRITE")) {
    Wire.beginTransmission((uint8_t)addr);
    size_t count = 0;
    for (const char *p = arg; p[0] && p[1]; p += 2) { Wire.write((uint8_t)((hexval(p[0]) << 4) | hexval(p[1]))); ++count; }
    const uint8_t rc = Wire.endTransmission();
    Serial.print("WRITE rc="); Serial.print(rc); Serial.print(" route="); Serial.print(route);
    Serial.print(" hz="); Serial.print(hz); Serial.print(" n="); Serial.println(count);
  } else if (!strcmp(verb, "READ")) {
    const size_t want = strtoul(arg, nullptr, 10);
    const size_t got = Wire.requestFrom((uint8_t)addr, want);
    Serial.print("READ got="); Serial.print(got); Serial.print(" data=");
    while (Wire.available()) { const int b = Wire.read(); if (b < 16) Serial.print('0'); Serial.print(b, HEX); }
    Serial.println();
  } else { Serial.print("ERR verb "); Serial.println(verb); }
}
