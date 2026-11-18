/* UART contracts (tests/instrumented/uart): Serial on the probe's UART and
 * lines, a SoftwareSerial on two plain lines. Console on SerialDMSeq; every
 * command answers one line (see each branch below):
 *
 *   uart, stdio, ucfg, upins, usend, urecv, uerr, uhold, uoff   Serial itself
 *   uwrite, uflow, urts                                          flow control
 *   u485, ude                                                    RS485's DE
 *   uirda                                                        IrDA (setMode)
 *   uhd, uavail                                                  single wire
 *   uevt, uevq                                                   onReceive()
 *   ssbegin, sssend, ssrecv, ssend                               SoftwareSerial
 *
 * Split from core_contracts (2026-11-17) so that the modes and SoftwareSerial
 * fit CH32V003's 16 KB. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <SoftwareSerial.h>
#include <new>
#include "internal/ch32rv_gpio.h"   // CH32RV_GPIO_NO_OPEN_DRAIN: whether a one-wire pad can be open drain
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

uint32_t requests;
char line[80];
uint8_t used;
volatile uint8_t receiveErrors;

// uevt / uevq: what Serial.onReceive() saw - how many calls, the bytes available at each (first 8), all the bytes it
// read (count and FNV-1a, which uevq continues over what is left).
uint8_t eventCalls;
uint8_t eventSeen[8];
uint32_t eventTotal;
uint32_t eventHash = 2166136261u;

// ssbegin: the SoftwareSerial under test, built in place (its pins are the constructor's).
alignas(SoftwareSerial) uint8_t softStorage[sizeof(SoftwareSerial)];
SoftwareSerial *soft;
static const uint16_t formats[] = {SERIAL_8N1, SERIAL_8E1, SERIAL_8O1, SERIAL_8N2, SERIAL_7E1, SERIAL_7O1};

uint32_t fnv(uint32_t hash, uint8_t byte) { return (hash ^ byte) * 16777619u; }

// Every byte that comes on `port` until `quietMs` pass with none: count and FNV-1a.
void drain(Stream &port, unsigned long quietMs, unsigned long &count, uint32_t &hash) {
  unsigned long last = millis();
  while (millis() - last < quietMs) {
    if (port.available()) {
      hash = fnv(hash, (uint8_t)port.read());
      ++count;
      last = millis();
    }
  }
}

void reply(const char *command) {
  ++requests;
  if (strncmp(command, "ready ", 6) == 0) {
    SerialDMSeq.print("READY ");
    SerialDMSeq.print(command + 6);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(BENCH_BUILD_ID);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(requests);
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
  } else if (strncmp(command, "ucfg ", 5) == 0) {
    // ucfg <baud> <format>: Serial again at that baud and format (0 8N1, 1 8E1, 2 8O1, 3 8N2, 4 7E1, 5 7O1).
    // UCFG <ok> <the baud the line runs at, Serial.baudRate()>.
    char *end;
    const unsigned long baud = strtoul(command + 5, &end, 10);
    const unsigned long k = strtoul(end, nullptr, 10);
    const bool ok = k < sizeof formats / sizeof formats[0];
    if (ok) {
      Serial.end();
      Serial.begin(baud, formats[k]);
    }
    SerialDMSeq.print("UCFG ");
    SerialDMSeq.print(ok && Serial ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(Serial.baudRate());
  } else if (strncmp(command, "upins ", 6) == 0) {
    // upins <rx> <tx>: Serial again on those pads (a WCH-Link whose UART reaches other pads than the default).
    char *end;
    const int rx = (int)strtol(command + 6, &end, 10);
    const int tx = (int)strtol(end, nullptr, 10);
    Serial.end();
    const bool ok = Serial.setPins(rx, tx);
    Serial.begin(115200);
    SerialDMSeq.print("UPINS ");
    SerialDMSeq.println(ok && Serial ? 1 : 0);
  } else if (strncmp(command, "usend ", 6) == 0) {
    // usend <n>: n bytes on Serial, byte i = (i * 37 + 11) & 0xFF (the host masks 7-bit formats itself).
    const unsigned long n = strtoul(command + 6, nullptr, 10);
    for (unsigned long i = 0; i < n; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    Serial.flush();
    SerialDMSeq.print("USEND ");
    SerialDMSeq.println(n);
  } else if (strncmp(command, "urecv ", 6) == 0) {
    // urecv <n> <ms>: up to n bytes from Serial within ms, then URECV <count> <FNV-1a 32 of them, hex>.
    char *end;
    const unsigned long n = strtoul(command + 6, &end, 10);
    const unsigned long ms = strtoul(end, nullptr, 10);
    while (Serial.available()) Serial.read();
    SerialDMSeq.println("URECV READY");
    uint32_t hash = 2166136261u;
    unsigned long got = 0;
    const unsigned long start = millis();
    while (got < n && millis() - start < ms) {
      if (Serial.available()) {
        hash = (hash ^ (uint8_t)Serial.read()) * 16777619u;
        ++got;
      }
    }
    SerialDMSeq.print("URECV ");
    SerialDMSeq.print(got);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(hash, HEX);
  } else if (strcmp(command, "uerr") == 0) {
    // uerr: UERR <mask> of what Serial.onReceiveError() reported since the last uerr (bit k: hardwareSerial_error_t k),
    // cleared. The first uerr registers the callback, so it reports 0.
    static bool registered;
    if (!registered) {
      Serial.onReceiveError([](hardwareSerial_error_t kind) { receiveErrors |= 1u << kind; });
      registered = true;
    }
    SerialDMSeq.print("UERR ");
    SerialDMSeq.println(receiveErrors);
    receiveErrors = 0;
  } else if (strncmp(command, "uhold ", 6) == 0) {
    // uhold <ms>: Serial not read for ms (after what came before is dropped), then UHOLD <available()>.
    while (Serial.available()) Serial.read();
    SerialDMSeq.println("UHOLD READY");
    delay(strtoul(command + 6, nullptr, 10));
    SerialDMSeq.print("UHOLD ");
    SerialDMSeq.println(Serial.available());
    while (Serial.available()) Serial.read();
  } else if (strncmp(command, "uoff ", 5) == 0) {
    // uoff <ms>: interrupts off for at least ms, so bytes coming meanwhile overrun the USART's data register. Counted
    // in loop turns (each at least 4 cycles): micros() stands still without the SysTick interrupt.
    const unsigned long ms = strtoul(command + 5, nullptr, 10);
    SerialDMSeq.println("UOFF READY");
    SerialDMSeq.flush();
    noInterrupts();
    for (volatile uint32_t i = 0; i < F_CPU / 4000u * ms; i = i + 1) {
    }
    interrupts();
    SerialDMSeq.println("UOFF");
    while (Serial.available()) Serial.read();
  } else if (strncmp(command, "uwrite ", 7) == 0) {
    // uwrite <n>: usend's bytes without waiting for them to go out (CTS may hold them).
    const unsigned long n = strtoul(command + 7, nullptr, 10);
    for (unsigned long i = 0; i < n; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    SerialDMSeq.print("UWRITE ");
    SerialDMSeq.println(n);
  } else if (strncmp(command, "uflow ", 6) == 0) {
    // uflow cts <pin> | uflow rts <pin> <threshold> | uflow off: Serial's hardware flow control on that pad (refused
    // where it is not the USART's CTS / RTS on Serial's route), or none.
    bool ok;
    char *end;
    if (strncmp(command + 6, "cts ", 4) == 0) {
      const int pin = (int)strtol(command + 10, nullptr, 10);
      ok = Serial.setPins(-1, -1, pin, -1) && Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS);
    } else if (strncmp(command + 6, "rts ", 4) == 0) {
      const int pin = (int)strtol(command + 10, &end, 10);
      const uint8_t threshold = (uint8_t)strtoul(end, nullptr, 10);
      ok = Serial.setPins(-1, -1, -1, pin) && Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_RTS, threshold);
    } else {
      ok = Serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
    }
    SerialDMSeq.print("UFLOW ");
    SerialDMSeq.println(ok ? 1 : 0);
  } else if (strncmp(command, "urts ", 5) == 0) {
    // urts <ms>: Serial not read for ms (RTS flow control holds what comes), then URTS <available()> and the count and
    // FNV-1a of everything read until 20 ms pass with nothing.
    while (Serial.available()) Serial.read();
    SerialDMSeq.println("URTS READY");
    delay(strtoul(command + 5, nullptr, 10));
    const int held = Serial.available();
    unsigned long count = 0;
    uint32_t hash = 2166136261u;
    drain(Serial, 20, count, hash);
    SerialDMSeq.print("URTS ");
    SerialDMSeq.print(held);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(count);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(hash, HEX);
  } else if (strncmp(command, "u485 ", 5) == 0) {
    // u485 <pin> | u485 off: Serial in RS485 half duplex with that pin as the driver enable, or plain UART again.
    bool ok;
    if (strcmp(command + 5, "off") == 0) {
      ok = Serial.setMode(UART_MODE_UART);
    } else {
      const int pin = (int)strtol(command + 5, nullptr, 10);
      ok = Serial.setPins(-1, -1, -1, pin) && Serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
    }
    SerialDMSeq.print("U485 ");
    SerialDMSeq.println(ok ? 1 : 0);
  } else if (strncmp(command, "uirda ", 6) == 0) {
    // uirda <1 on | 0 off>: Serial in IrDA (setMode(UART_MODE_IRDA)), or plain UART again.
    const bool ok = Serial.setMode(command[6] == '1' ? UART_MODE_IRDA : UART_MODE_UART);
    SerialDMSeq.print("UIRDA ");
    SerialDMSeq.println(ok ? 1 : 0);
  } else if (strncmp(command, "ude ", 4) == 0) {
    // ude <n1> <gap ms> <n2>: usend's bytes in two writes with a pause between (two driver-enable windows).
    char *end;
    const unsigned long n1 = strtoul(command + 4, &end, 10);
    const unsigned long gap = strtoul(end, &end, 10);
    const unsigned long n2 = strtoul(end, nullptr, 10);
    unsigned long i = 0;
    for (; i < n1; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    Serial.flush();
    delay(gap);
    for (; i < n1 + n2; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    Serial.flush();
    SerialDMSeq.println("UDE");
  } else if (strncmp(command, "uhd ", 4) == 0) {
    // uhd <pin>: Serial at 9600 8N1 as single-wire half duplex on that pad (RX = TX). UHD <ok> <open drain> <baud>.
    const int pin = (int)strtol(command + 4, nullptr, 10);
    Serial.end();
    Serial.begin(9600, SERIAL_8N1, pin, pin);
    SerialDMSeq.print("UHD ");
    SerialDMSeq.print(Serial ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(CH32RV_GPIO_NO_OPEN_DRAIN ? 0 : 1);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(Serial.baudRate());
  } else if (strcmp(command, "uavail") == 0) {
    // uavail: the count and FNV-1a of what Serial holds (until 20 ms with nothing), drained.
    unsigned long count = 0;
    uint32_t hash = 2166136261u;
    drain(Serial, 20, count, hash);
    SerialDMSeq.print("UAVAIL ");
    SerialDMSeq.print(count);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(hash, HEX);
  } else if (strncmp(command, "uevt ", 5) == 0) {
    // uevt <onlyOnTimeout> <rx timeout symbols> <fifo full> | uevt off: Serial.onReceive() counting its calls, or none.
    eventCalls = 0;
    eventTotal = 0;
    eventHash = 2166136261u;
    while (Serial.available()) Serial.read();
    if (strcmp(command + 5, "off") == 0) {
      Serial.onReceive(nullptr);
    } else {
      char *end;
      const bool only = strtoul(command + 5, &end, 10) != 0;
      const uint8_t symbols = (uint8_t)strtoul(end, &end, 10);
      const uint8_t fifo = (uint8_t)strtoul(end, nullptr, 10);
      Serial.setRxTimeout(symbols);
      Serial.setRxFIFOFull(fifo);
      Serial.onReceive([]() {
        const int n = Serial.available();
        if (eventCalls < sizeof eventSeen) eventSeen[eventCalls] = n > 255 ? 255 : (uint8_t)n;
        ++eventCalls;
        while (Serial.available()) {
          eventHash = fnv(eventHash, (uint8_t)Serial.read());
          ++eventTotal;
        }
      }, only);
    }
    SerialDMSeq.println("UEVT 1");
  } else if (strcmp(command, "uevq") == 0) {
    // uevq: UEVQ <calls> <bytes the calls read> <bytes left> <FNV-1a over both> <available at each call, comma-separated>.
    unsigned long left = 0;
    uint32_t hash = eventHash;
    drain(Serial, 5, left, hash);
    SerialDMSeq.print("UEVQ ");
    SerialDMSeq.print(eventCalls);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(eventTotal);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(left);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(hash, HEX);
    SerialDMSeq.print(' ');
    for (uint8_t i = 0; i < eventCalls && i < sizeof eventSeen; ++i) {
      if (i) SerialDMSeq.print(',');
      SerialDMSeq.print(eventSeen[i]);
    }
    SerialDMSeq.println();
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
    SerialDMSeq.print("SSBEGIN ");
    SerialDMSeq.println(soft->isListening() ? 1 : 0);
  } else if (strncmp(command, "sssend ", 7) == 0) {
    // sssend <n>: usend's bytes on the SoftwareSerial.
    const unsigned long n = strtoul(command + 7, nullptr, 10);
    unsigned long sent = 0;
    for (unsigned long i = 0; soft && i < n; ++i) sent += soft->write((uint8_t)(i * 37u + 11u));
    SerialDMSeq.print("SSSEND ");
    SerialDMSeq.println(sent);
  } else if (strncmp(command, "ssrecv ", 7) == 0) {
    // ssrecv <n> <ms>: urecv on the SoftwareSerial; SSRECV <count> <FNV-1a> <overflow>.
    char *end;
    const unsigned long n = strtoul(command + 7, &end, 10);
    const unsigned long ms = strtoul(end, nullptr, 10);
    while (soft && soft->available()) soft->read();
    SerialDMSeq.println("SSRECV READY");
    uint32_t hash = 2166136261u;
    unsigned long got = 0;
    const unsigned long start = millis();
    while (soft && got < n && millis() - start < ms) {
      if (soft->available()) {
        hash = fnv(hash, (uint8_t)soft->read());
        ++got;
      }
    }
    SerialDMSeq.print("SSRECV ");
    SerialDMSeq.print(got);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(hash, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(soft && soft->overflow() ? 1 : 0);
  } else if (strcmp(command, "ssend") == 0) {
    if (soft) soft->end();
    SerialDMSeq.println("SSEND");
  } else if (strcmp(command, "stdio") == 0) {
    // stdout follows Serial once it is begun (setup()); ch32rv_set_stdout() overrides it, nullptr discards.
    puts("STDIO SERIAL");
    ch32rv_set_stdout(nullptr);
    puts("STDIO DISCARDED");
    ch32rv_set_stdout(&Serial);
    puts("STDIO BACK");
    fflush(stdout);
    Serial.flush();
    SerialDMSeq.println("STDIO DONE");
  } else {
    SerialDMSeq.println("ERROR COMMAND");
  }
}

void setup() {
  Serial.begin(115200);
  SerialDMSeq.begin(115200);
  SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
  SerialDMSeq.println(CH32RV.resetReasonName());
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
