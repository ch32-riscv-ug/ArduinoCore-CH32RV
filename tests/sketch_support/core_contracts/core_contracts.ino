#include <Arduino.h>
#include <SerialDMSeq.h>
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
    SerialDMSeq.print("READY ");
    SerialDMSeq.print(command + 6);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(BENCH_BUILD_ID);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(requests);
  } else if (strcmp(command, "init") == 0) {
    SerialDMSeq.print("INIT ");
    SerialDMSeq.print(dataMarker, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(bssMarker);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(ctorMarker, HEX);
  } else if (strcmp(command, "time") == 0) {
    unsigned long ms = millis();
    delay(50);
    unsigned long elapsedMs = millis() - ms;
    unsigned long us = micros();
    delayMicroseconds(1000);
    unsigned long elapsedUs = micros() - us;
    SerialDMSeq.print("TIME ");
    SerialDMSeq.print(elapsedMs);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(elapsedUs);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(F_CPU);
  } else if (strcmp(command, "print") == 0) {
    String value = String("ch32") + String(1234);
    SerialDMSeq.print("PRINT ");
    SerialDMSeq.print(value);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(0x1234abcdUL, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(-42);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(13, BIN);
  } else if (strcmp(command, "heap") == 0) {
    uint8_t *p = static_cast<uint8_t *>(malloc(32));
    unsigned sum = 0;
    if (p) {
      for (unsigned i = 0; i < 32; ++i) p[i] = i;
      for (unsigned i = 0; i < 32; ++i) sum += p[i];
      free(p);
    }
    SerialDMSeq.print("HEAP ");
    SerialDMSeq.println(sum);
  } else if (strcmp(command, "uart") == 0) {
    while (Serial.available()) Serial.read();
    SerialDMSeq.println("UART READY");
    uint8_t bytes[64];
    size_t n = 0;
    unsigned long start = millis();
    while (n < sizeof(bytes) && millis() - start < 3000) {
      if (Serial.available()) bytes[n++] = Serial.read();
    }
    Serial.write(bytes, n);
    Serial.flush();
    SerialDMSeq.print("UART COUNT ");
    SerialDMSeq.println(n);
  } else if (strncmp(command, "gpio ", 5) == 0) {
    char *end;
    unsigned long rawPin = strtoul(command + 5, &end, 10);
    if (rawPin < 64 || rawPin >= 255) {
      SerialDMSeq.println("ERROR PIN");
      return;
    }
    pin_size_t pin = static_cast<pin_size_t>(rawPin);
    if (strcmp(end, " in") == 0) {
      pinMode(pin, INPUT);
      SerialDMSeq.print("GPIO IN ");
      SerialDMSeq.println(digitalRead(pin));
    } else if (strcmp(end, " out0") == 0 || strcmp(end, " out1") == 0) {
      // Preload the level before enabling an output.
      digitalWrite(pin, end[4] == '1' ? HIGH : LOW);
      pinMode(pin, OUTPUT);
      SerialDMSeq.println("GPIO OUT");
    } else {
      SerialDMSeq.println("ERROR MODE");
    }
  } else {
    SerialDMSeq.println("ERROR COMMAND");
  }
}

void setup() {
  Serial.begin(115200);
  SerialDMSeq.begin(115200);
}

void loop() {
  while (SerialDMSeq.available()) {
    int c = SerialDMSeq.read();
    if (c == '\n') {
      line[used] = 0;
      if (used) reply(line);
      used = 0;
    } else if (c != '\r' && used < sizeof(line) - 1) {
      line[used++] = c;
    }
  }
}
