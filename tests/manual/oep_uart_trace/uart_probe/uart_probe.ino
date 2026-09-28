// One UART under test, UUT_SERIAL (default Serial2: X035 PA2 TX / PA3 RX -> P4 GPIO48 / GPIO49; the sweep builds with
// -DUUT_SERIAL=Serial<n> for the profile's UART), exercised on request from the console (target.console).
//   OPEN <baud> [<fmt>]    UUT_SERIAL.begin(baud, fmt), fmt 8N1 (default) 8E1 8O1 8N2 7E1 ... -> "OPEN ok"
//   CLOCK                  -> "CLOCK f_cpu=<F_CPU>"
//   SWITCH <baud> <seed>   16 bytes at the open baud, begin(baud) with them still leaving, 16 more -> "SWITCH done"
//   CLOSE                  UUT_SERIAL.end()                             -> "CLOSE ok"
//   SEND <n> <seed>        write n LCG bytes                            -> "SEND done"
//   ECHO <n> <ms>          echo up to n bytes as they arrive, ms idle timeout -> "ECHO n=<k>"
//   RECV <n> <delay_ms>    wait delay_ms without reading (overflow), then drain for 200 ms -> "RECV n=<k> sum=<s> avail_peak=<p>"
//   BURST <ms> <seed>      16 LCG bytes, the line idle for 20 bit times, again, for ms -> "BURST blocks=<k>"
//                          (a window captured anywhere inside holds whole frames: the probe capture has no trigger)
// LCG: x = x * 1103515245 + 12345; byte = x >> 16.
#define TC_CMD_MAX 64
#include "testcmd.h"

#ifndef UUT_SERIAL
#define UUT_SERIAL Serial2
#endif
static unsigned long open_baud = 0;

// "8E1" -> SERIAL_8E1: data bits, parity (N / E / O), stop bits
static uint16_t serial_config(const char *f) {
  if (!f[0] || !f[1] || !f[2]) return SERIAL_8N1;
  const uint16_t data = f[0] == '7' ? SERIAL_DATA_7 : SERIAL_DATA_8;
  const uint16_t parity = f[1] == 'E' ? SERIAL_PARITY_EVEN : f[1] == 'O' ? SERIAL_PARITY_ODD : SERIAL_PARITY_NONE;
  const uint16_t stop = f[2] == '2' ? SERIAL_STOP_BIT_2 : SERIAL_STOP_BIT_1;
  return data | parity | stop;
}

static uint32_t lcg(uint32_t &x) { x = x * 1103515245u + 12345u; return (x >> 16) & 0xff; }

void setup() { tc_begin("uart_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned long a = 0, b = 0;
  const int n = sscanf(cmd, "%7s %lu %lu", verb, &a, &b);
  if (n < 1) return;
  if (!strcmp(verb, "OPEN")) {
    char fmt[4] = "8N1";
    sscanf(cmd, "%*s %*lu %3s", fmt);
    UUT_SERIAL.begin(a, serial_config(fmt)); open_baud = a; Console.println("OPEN ok");
  }
  else if (!strcmp(verb, "SWITCH")) {
    // 16 LCG bytes of seed at the open baud, begin(a) straight after write() (no flush), 16 of seed + 1 at a
    uint32_t x = (uint32_t)b;
    for (int i = 0; i < 16; ++i) UUT_SERIAL.write((uint8_t)lcg(x));
    UUT_SERIAL.begin(a); open_baud = a;
    x = (uint32_t)b + 1;
    for (int i = 0; i < 16; ++i) UUT_SERIAL.write((uint8_t)lcg(x));
    UUT_SERIAL.flush(); Console.println("SWITCH done");
  }
  else if (!strcmp(verb, "CLOCK")) { Console.print("CLOCK f_cpu="); Console.println((unsigned long)F_CPU); }
  else if (!strcmp(verb, "BURST")) {
    const unsigned long gap_us = open_baud ? 20000000ul / open_baud + 1 : 100;
    unsigned long blocks = 0; const unsigned long t0 = millis();
    while (millis() - t0 < a) {
      uint32_t x = (uint32_t)b;
      for (int i = 0; i < 16; ++i) UUT_SERIAL.write((uint8_t)lcg(x));
      UUT_SERIAL.flush(); delayMicroseconds(gap_us); ++blocks;
    }
    Console.print("BURST blocks="); Console.println(blocks);
  }
  else if (!strcmp(verb, "CLOSE")) { UUT_SERIAL.end(); Console.println("CLOSE ok"); }
  else if (!strcmp(verb, "SEND")) {
    uint32_t x = (uint32_t)b;
    for (unsigned long i = 0; i < a; ++i) UUT_SERIAL.write((uint8_t)lcg(x));
    UUT_SERIAL.flush(); Console.println("SEND done");
  } else if (!strcmp(verb, "ECHO")) {
    unsigned long k = 0; unsigned long last = millis();
    while (k < a && (millis() - last) < b) {
      while (UUT_SERIAL.available() && k < a) { UUT_SERIAL.write((uint8_t)UUT_SERIAL.read()); ++k; last = millis(); }
    }
    UUT_SERIAL.flush(); Console.print("ECHO n="); Console.println(k);
  } else if (!strcmp(verb, "RECV")) {
    delay(b);
    int peak = UUT_SERIAL.available();
    unsigned long k = 0, sum = 0; const unsigned long t0 = millis();
    while (millis() - t0 < 200) {
      const int av = UUT_SERIAL.available(); if (av > peak) peak = av;
      while (UUT_SERIAL.available() && k < a) { sum += (uint8_t)UUT_SERIAL.read(); ++k; }
    }
    Console.print("RECV n="); Console.print(k); Console.print(" sum="); Console.print(sum); Console.print(" avail_peak="); Console.println(peak);
  } else { Console.print("ERR verb "); Console.println(verb); }
}
