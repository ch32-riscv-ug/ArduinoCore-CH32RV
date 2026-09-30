// I2C master transactions on request, for tests/bench/trace/i2c_probe: the host drives the sketch over the
// console and watches the bus with the probe's capture and I2C target, so the sketch only reports what the
// core's Wire returned.
//   PINS <scl> <sda>                            the route's pads as the core's pin numbers (for STUCK / BUSCLR)
//   BEGIN <route>                               Wire.setRoute + Wire.begin           -> "BEGIN route=<r>"
//   WRITE <route> <hz> <addr_hex> <bytes_hex>   -> "WRITE rc=<n> route=<r> hz=<hz> n=<bytes> t_us=<t> timeout=<0|1>"
//   READ  <route> <hz> <addr_hex> <count>       -> "READ got=<n> data=<hex> t_us=<t> timeout=<0|1>"
//   WRREAD <route> <hz> <addr_hex> <bytes_hex>  write without STOP, then read 4     -> "WRREAD rc=<n> got=<n> data=<hex>"
//   STUCK <addr_hex>                            leave a target driving SDA low mid-byte -> "STUCK ack=.. byte1=.. sda=.. scl=.."
//   BUSCLR                                      Wire.clearBus()                      -> "BUSCLR free=<0|1> sda=.. scl=.."
// The bit-banged masters of the investigation (BB / BBF) stayed in tests/manual/oep_i2c_trace.
#define TC_CMD_MAX 96
#include "testcmd.h"
#include <Wire.h>

static uint8_t hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

// BEGIN <route> re-opens the bus (Wire.begin) on purpose; WRITE/READ only set the clock,
// so a trace can separate "first transaction after begin()" from a settled bus.
static int gRoute = -1;
static bool selectRoute(uint8_t route, unsigned long hz) {
  if (gRoute != (int)route) {
    if (!Wire.setRoute(route)) { Console.print("ERR route "); Console.println(route); return false; }
    gRoute = route;
    Wire.begin();
  }
  Wire.setClock(hz);
  return true;
}

// The route's pads, for the stuck-bus scenario that bit-bangs them: PINS sets them (defaults: X035 route 2).
static uint8_t BB_SCL = PC16, BB_SDA = PC17;
// Stuck-bus scenario (worklist P3 row 6): STUCK <addr_hex> bit-bangs START + addr|R, ACKs the first
// data byte and then stops clocking with SCL high while the target drives the next byte's MSB. With a
// preloaded 0x00 slot the target keeps SDA low: the classic "slave holds SDA" bus hang for Wire to meet.
// BUSCLR calls Wire.clearBus(): up to 9 SCL pulses with SDA released, then STOP.
// The fixture's route 2 has no bus pull-up at all (P4 slave driver enables none, no external ones;
// 2026-09-22 capture: X035 INPUT / open-drain release read 0.00), so STUCK drives SCL push-pull and
// releases SDA as INPUT_PULLUP, the same way the BB drive=pp sweep does it; clearBus() releases both
// lines with the internal pull-up and only ever drives them low. STUCK leaves the pins as GPIO and so
// does clearBus() when Wire was not open; send BEGIN to hand them back to Wire.
static void sdaRelease() { pinMode(BB_SDA, INPUT_PULLUP); }
static void sdaLow() { pinMode(BB_SDA, OUTPUT); digitalWrite(BB_SDA, LOW); }
static void sclHigh() { pinMode(BB_SCL, OUTPUT); digitalWrite(BB_SCL, HIGH); }
static void sclLow() { pinMode(BB_SCL, OUTPUT); digitalWrite(BB_SCL, LOW); }
static void stuckBus(uint8_t addr) {
  Wire.end(); gRoute = -1;   // the peripheral must not watch the bus through the input path while we bit-bang
  sdaRelease(); sclHigh(); delayMicroseconds(20);
  sdaLow(); delayMicroseconds(5);                                              // START
  const uint8_t byte0 = (uint8_t)((addr << 1) | 1);
  for (int i = 7; i >= 0; --i) {
    sclLow(); delayMicroseconds(2);
    if ((byte0 >> i) & 1) sdaRelease(); else sdaLow();
    delayMicroseconds(3); sclHigh(); delayMicroseconds(5);
  }
  sclLow(); sdaRelease(); delayMicroseconds(5);                                // ACK slot
  sclHigh(); delayMicroseconds(2); const char ack = digitalRead(BB_SDA) ? 'N' : 'A'; delayMicroseconds(3);
  uint8_t data = 0;
  for (int i = 7; i >= 0; --i) {                                                // first data byte, target drives
    sclLow(); delayMicroseconds(5); sclHigh(); delayMicroseconds(2);
    data = (uint8_t)((data << 1) | (digitalRead(BB_SDA) ? 1 : 0)); delayMicroseconds(3);
  }
  sclLow(); delayMicroseconds(1); sdaLow(); delayMicroseconds(4);              // master ACK
  sclHigh(); delayMicroseconds(5);
  sclLow(); delayMicroseconds(1); sdaRelease(); delayMicroseconds(4);          // target now drives MSB of byte 2
  sclHigh(); delayMicroseconds(20);                                             // ... and we walk away with SCL high
  Console.print("STUCK ack="); Console.print(ack); Console.print(" byte1="); Console.print(data, HEX);
  Console.print(" sda="); Console.print(digitalRead(BB_SDA)); Console.print(" scl="); Console.println(digitalRead(BB_SCL));
}
static void busClear() {
  const bool free = Wire.clearBus();   // the library's bus clear (up to 9 SCL pulses, then STOP)
  Console.print("BUSCLR free="); Console.print(free ? 1 : 0); Console.print(" sda="); Console.print(digitalRead(BB_SDA));
  Console.print(" scl="); Console.println(digitalRead(BB_SCL));
}

void setup() { tc_begin("i2c_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8]; unsigned route, addr; unsigned long hz; char arg[72] = {0};
  const int n = sscanf(cmd, "%7s %u %lu %x %71s", verb, &route, &hz, &addr, arg);
  if (n >= 2 && !strcmp(verb, "BEGIN")) {
    if (!Wire.setRoute((uint8_t)route)) { Console.print("ERR route "); Console.println(route); return; }
    gRoute = route; Wire.begin();
    Console.print("BEGIN route="); Console.println(route);
    return;
  }
  if (n >= 1 && !strcmp(verb, "BUSCLR")) { busClear(); return; }
  if (n >= 2 && !strcmp(verb, "STUCK")) {
    unsigned a = 0; if (sscanf(cmd, "%*s %x", &a) < 1) { Console.println("ERR STUCK usage"); return; }
    stuckBus((uint8_t)a); return;
  }
  if (n >= 3 && !strcmp(verb, "PINS")) {   // PINS <scl> <sda>: sscanf mapped scl -> route, sda -> hz
    BB_SCL = (uint8_t)route; BB_SDA = (uint8_t)hz; Console.println("PINS ok"); return;
  }
  if (n < 5) { Console.print("ERR usage: "); Console.println(cmd); return; }
  if (!selectRoute((uint8_t)route, hz)) return;
  if (!strcmp(verb, "WRITE")) {
    Wire.beginTransmission((uint8_t)addr);
    size_t count = 0;
    for (const char *p = arg; p[0] && p[1]; p += 2) { Wire.write((uint8_t)((hexval(p[0]) << 4) | hexval(p[1]))); ++count; }
    const uint32_t t0 = micros();
    const uint8_t rc = Wire.endTransmission();
    const uint32_t t_us = micros() - t0;
    Console.print("WRITE rc="); Console.print(rc); Console.print(" route="); Console.print(route);
    Console.print(" hz="); Console.print(hz); Console.print(" n="); Console.print(count);
    Console.print(" t_us="); Console.print(t_us); Console.print(" timeout="); Console.println(Wire.getWireTimeoutFlag() ? 1 : 0);
    Wire.clearWireTimeoutFlag();
  } else if (!strcmp(verb, "WRREAD")) {
    // write <bytes_hex> without STOP, then requestFrom 4 bytes: repeated START on the wire
    Wire.beginTransmission((uint8_t)addr);
    for (const char *p = arg; p[0] && p[1]; p += 2) Wire.write((uint8_t)((hexval(p[0]) << 4) | hexval(p[1])));
    const uint8_t rc = Wire.endTransmission(false);
    const size_t got = Wire.requestFrom((uint8_t)addr, (size_t)4, true);
    Console.print("WRREAD rc="); Console.print(rc); Console.print(" got="); Console.print(got); Console.print(" data=");
    while (Wire.available()) { const int b = Wire.read(); if (b < 16) Console.print('0'); Console.print(b, HEX); }
    Console.println();
  } else if (!strcmp(verb, "READ")) {
    const size_t want = strtoul(arg, nullptr, 10);
    const uint32_t t0 = micros();
    const size_t got = Wire.requestFrom((uint8_t)addr, want);
    const uint32_t t_us = micros() - t0;
    Console.print("READ got="); Console.print(got); Console.print(" data=");
    while (Wire.available()) { const int b = Wire.read(); if (b < 16) Console.print('0'); Console.print(b, HEX); }
    Console.print(" t_us="); Console.print(t_us); Console.print(" timeout="); Console.println(Wire.getWireTimeoutFlag() ? 1 : 0);
    Wire.clearWireTimeoutFlag();
  } else { Console.print("ERR verb "); Console.println(verb); }
}
