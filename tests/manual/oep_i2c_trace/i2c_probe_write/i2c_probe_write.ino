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
