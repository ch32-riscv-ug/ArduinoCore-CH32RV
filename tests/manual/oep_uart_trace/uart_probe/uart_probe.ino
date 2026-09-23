// USART2 (PA2 TX / PA3 RX -> P4 GPIO48 / GPIO49) exercised on request from the console (USART4).
//   OPEN <baud>            Serial2.begin(baud)                          -> "OPEN ok"
//   CLOSE                  Serial2.end()                                -> "CLOSE ok"
//   SEND <n> <seed>        write n LCG bytes                            -> "SEND done"
//   ECHO <n> <ms>          echo up to n bytes as they arrive, ms idle timeout -> "ECHO n=<k>"
//   RECV <n> <delay_ms>    wait delay_ms without reading (overflow), then drain for 200 ms -> "RECV n=<k> sum=<s> avail_peak=<p>"
// LCG: x = x * 1103515245 + 12345; byte = x >> 16.
#define TC_CMD_MAX 64
#include "testcmd.h"

static uint32_t lcg(uint32_t &x) { x = x * 1103515245u + 12345u; return (x >> 16) & 0xff; }

void setup() { tc_begin("uart_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned long a = 0, b = 0;
  const int n = sscanf(cmd, "%7s %lu %lu", verb, &a, &b);
  if (n < 1) return;
  if (!strcmp(verb, "OPEN")) { Serial2.begin(a); Console.println("OPEN ok"); }
  else if (!strcmp(verb, "CLOSE")) { Serial2.end(); Console.println("CLOSE ok"); }
  else if (!strcmp(verb, "SEND")) {
    uint32_t x = (uint32_t)b;
    for (unsigned long i = 0; i < a; ++i) Serial2.write((uint8_t)lcg(x));
    Serial2.flush(); Console.println("SEND done");
  } else if (!strcmp(verb, "ECHO")) {
    unsigned long k = 0; unsigned long last = millis();
    while (k < a && (millis() - last) < b) {
      while (Serial2.available() && k < a) { Serial2.write((uint8_t)Serial2.read()); ++k; last = millis(); }
    }
    Serial2.flush(); Console.print("ECHO n="); Console.println(k);
  } else if (!strcmp(verb, "RECV")) {
    delay(b);
    int peak = Serial2.available();
    unsigned long k = 0, sum = 0; const unsigned long t0 = millis();
    while (millis() - t0 < 200) {
      const int av = Serial2.available(); if (av > peak) peak = av;
      while (Serial2.available() && k < a) { sum += (uint8_t)Serial2.read(); ++k; }
    }
    Console.print("RECV n="); Console.print(k); Console.print(" sum="); Console.print(sum); Console.print(" avail_peak="); Console.println(peak);
  } else { Console.print("ERR verb "); Console.println(verb); }
}
