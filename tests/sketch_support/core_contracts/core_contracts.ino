#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

volatile uint32_t dataMarker = 0x13579bdf;
volatile uint32_t bssMarker;
volatile uint32_t ctorMarker;
struct Constructor {
  Constructor() { ctorMarker = 0x2468ace0; }
} constructor;
uint32_t requests;
char line[80];
uint8_t used;

void reply(const char *command) {
  ++requests;
  if (strncmp(command, "ready ", 6) == 0) {
    CONSOLE.print("READY ");
    CONSOLE.print(command + 6);
    CONSOLE.print(' ');
    CONSOLE.print(BENCH_BUILD_ID);
    CONSOLE.print(' ');
    CONSOLE.println(requests);
  } else if (strcmp(command, "init") == 0) {
    CONSOLE.print("INIT ");
    CONSOLE.print(dataMarker, HEX);
    CONSOLE.print(' ');
    CONSOLE.print(bssMarker);
    CONSOLE.print(' ');
    CONSOLE.println(ctorMarker, HEX);
  } else if (strcmp(command, "time") == 0) {
    unsigned long ms = millis();
    delay(50);
    unsigned long elapsedMs = millis() - ms;
    unsigned long us = micros();
    delayMicroseconds(1000);
    unsigned long elapsedUs = micros() - us;
    CONSOLE.print("TIME ");
    CONSOLE.print(elapsedMs);
    CONSOLE.print(' ');
    CONSOLE.print(elapsedUs);
    CONSOLE.print(' ');
    CONSOLE.println(F_CPU);
  } else if (strcmp(command, "print") == 0) {
    String value = String("ch32") + String(1234);
    CONSOLE.print("PRINT ");
    CONSOLE.print(value);
    CONSOLE.print(' ');
    CONSOLE.print(0x1234abcdUL, HEX);
    CONSOLE.print(' ');
    CONSOLE.print(-42);
    CONSOLE.print(' ');
    CONSOLE.println(13, BIN);
  } else if (strcmp(command, "heap") == 0) {
    uint8_t *p = static_cast<uint8_t *>(malloc(32));
    unsigned sum = 0;
    if (p) {
      for (unsigned i = 0; i < 32; ++i) p[i] = i;
      for (unsigned i = 0; i < 32; ++i) sum += p[i];
      free(p);
    }
    CONSOLE.print("HEAP ");
    CONSOLE.println(sum);
  } else if (strncmp(command, "gpio ", 5) == 0) {
    char *end;
    unsigned long rawPin = strtoul(command + 5, &end, 10);
    if (rawPin < 64 || rawPin >= 255) {
      CONSOLE.println("ERROR PIN");
      return;
    }
    pin_size_t pin = static_cast<pin_size_t>(rawPin);
    if (strcmp(end, " in") == 0) {
      pinMode(pin, INPUT);
      CONSOLE.print("GPIO IN ");
      CONSOLE.println(digitalRead(pin));
    } else if (strcmp(end, " out0") == 0 || strcmp(end, " out1") == 0) {
      // Preload the level before enabling an output.
      digitalWrite(pin, end[4] == '1' ? HIGH : LOW);
      pinMode(pin, OUTPUT);
      CONSOLE.println("GPIO OUT");
    } else {
      CONSOLE.println("ERROR MODE");
    }
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
