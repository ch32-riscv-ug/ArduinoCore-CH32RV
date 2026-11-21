/* SoftwareSerial (tests/instrumented/software_serial): a SoftwareSerial on two
 * plain lines, against the probe's UART. Console on CONSOLE (bench_console.h); every command
 * answers one line (see each branch below):
 *
 *   ssbegin, sssend, ssrecv, ssend
 *
 * Split from sketch_support/uart (2026-11-19) so that uart.ino fits
 * CH32V003's 16 KB with the multiprocessor mode. */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <SoftwareSerial.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

uint32_t requests;
char line[80];
uint8_t used;

// ssbegin: the SoftwareSerial under test, built in place (its pins are the constructor's).
alignas(SoftwareSerial) uint8_t softStorage[sizeof(SoftwareSerial)];
SoftwareSerial *soft;
// ssbegin's formats: 0 8N1, 1 8E1, 2 8O1, 3 8N2, 4 7E1, 5 7O1 (uart.ino's ucfg).
static const uint16_t formats[] = {SERIAL_8N1, SERIAL_8E1, SERIAL_8O1, SERIAL_8N2, SERIAL_7E1, SERIAL_7O1};

uint32_t fnv(uint32_t hash, uint8_t byte) { return (hash ^ byte) * 16777619u; }

void reply(const char *command) {
  ++requests;
  if (strncmp(command, "ready ", 6) == 0) {
    CONSOLE.print("READY ");
    CONSOLE.print(command + 6);
    CONSOLE.print(' ');
    CONSOLE.print(BENCH_BUILD_ID);
    CONSOLE.print(' ');
    CONSOLE.println(requests);
  } else if (strncmp(command, "ssbegin ", 8) == 0) {
    // ssbegin <rx> <tx> <baud> <format>: a SoftwareSerial on those pads (ucfg's formats). SSBEGIN <listening>.
    char *end;
    const int rx = (int)strtol(command + 8, &end, 10);
    const int tx = (int)strtol(end, &end, 10);
    const long baud = strtol(end, &end, 10);
    const unsigned long k = strtoul(end, nullptr, 10);
    if (soft) soft->~SoftwareSerial();
    soft = new (softStorage) SoftwareSerial((uint8_t)rx, (uint8_t)tx);
    soft->begin(baud, k < sizeof formats / sizeof formats[0] ? formats[k] : 0);
    CONSOLE.print("SSBEGIN ");
    CONSOLE.println(soft->isListening() ? 1 : 0);
  } else if (strncmp(command, "sssend ", 7) == 0) {
    // sssend <n>: usend's bytes on the SoftwareSerial.
    const unsigned long n = strtoul(command + 7, nullptr, 10);
    unsigned long sent = 0;
    for (unsigned long i = 0; soft && i < n; ++i) sent += soft->write((uint8_t)(i * 37u + 11u));
    CONSOLE.print("SSSEND ");
    CONSOLE.println(sent);
  } else if (strncmp(command, "ssrecv ", 7) == 0) {
    // ssrecv <n> <ms>: urecv on the SoftwareSerial; SSRECV <count> <FNV-1a> <overflow>.
    char *end;
    const unsigned long n = strtoul(command + 7, &end, 10);
    const unsigned long ms = strtoul(end, nullptr, 10);
    while (soft && soft->available()) soft->read();
    CONSOLE.println("SSRECV READY");
    uint32_t hash = 2166136261u;
    unsigned long got = 0;
    const unsigned long start = millis();
    while (soft && got < n && millis() - start < ms) {
      if (soft->available()) {
        hash = fnv(hash, (uint8_t)soft->read());
        ++got;
      }
    }
    CONSOLE.print("SSRECV ");
    CONSOLE.print(got);
    CONSOLE.print(' ');
    CONSOLE.print(hash, HEX);
    CONSOLE.print(' ');
    CONSOLE.println(soft && soft->overflow() ? 1 : 0);
  } else if (strcmp(command, "ssend") == 0) {
    if (soft) soft->end();
    CONSOLE.println("SSEND");
  } else {
    CONSOLE.println("ERROR COMMAND");
  }
}

void setup() {
  CONSOLE_BEGIN(115200);
  CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
  CONSOLE.println(CH32RV.resetReasonName());
}

void loop() {
  while (CONSOLE.available()) {
    int c = CONSOLE.read();
    if (c == '\n') {
      line[used] = 0;
      if (used) reply(line);
      used = 0;
    } else if (c != '\r' && used < sizeof(line) - 1) {
      line[used++] = c;
    }
  }
}
