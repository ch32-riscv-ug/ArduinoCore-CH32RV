/* UART contracts (tests/instrumented/uart): Serial on the probe's UART and
 * lines. Console on CONSOLE (bench_console.h); every
 * command answers one line (see each branch below):
 *
 *   uart, stdio, ucfg, upins, usend, urecv, uerr, uhold, uoff   Serial itself
 *   uwrite, uflow, urts                                          flow control
 *   u485, ude                                                    RS485's DE
 *   uirda                                                        IrDA (setMode)
 *   ulin                                                         LIN (setMode, sendBreak)
 *   uaddr, umsg, umute                                           multiprocessor (mute mode)
 *   uhd, uavail                                                  single wire
 *   uevt, uevq                                                   onReceive()
 *
 * Split from core_contracts (2026-11-17) so that the modes fit CH32V003's
 * 16 KB; SoftwareSerial went to sketch_support/software_serial (2026-11-19)
 * to make room for the multiprocessor mode. */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
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
    CONSOLE.print("READY ");
    CONSOLE.print(command + 6);
    CONSOLE.print(' ');
    CONSOLE.print(BENCH_BUILD_ID);
    CONSOLE.print(' ');
    CONSOLE.println(requests);
  } else if (strcmp(command, "uart") == 0) {
    while (Serial.available()) Serial.read();
    CONSOLE.println("UART READY");
    uint8_t bytes[64];
    size_t n = 0;
    unsigned long start = millis();
    while (n < sizeof(bytes) && millis() - start < 3000) {
      if (Serial.available()) bytes[n++] = Serial.read();
    }
    Serial.write(bytes, n);
    Serial.flush();
    CONSOLE.print("UART COUNT ");
    CONSOLE.println(n);
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
    CONSOLE.print("UCFG ");
    CONSOLE.print(ok && Serial ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println(Serial.baudRate());
  } else if (strncmp(command, "upins ", 6) == 0) {
    // upins <rx> <tx>: Serial again on those pads (a WCH-Link whose UART reaches other pads than the default).
    char *end;
    const int rx = (int)strtol(command + 6, &end, 10);
    const int tx = (int)strtol(end, nullptr, 10);
    Serial.end();
    const bool ok = Serial.setPins(rx, tx);
    Serial.begin(115200);
    CONSOLE.print("UPINS ");
    CONSOLE.println(ok && Serial ? 1 : 0);
  } else if (strncmp(command, "usend ", 6) == 0) {
    // usend <n>: n bytes on Serial, byte i = (i * 37 + 11) & 0xFF (the host masks 7-bit formats itself).
    const unsigned long n = strtoul(command + 6, nullptr, 10);
    for (unsigned long i = 0; i < n; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    Serial.flush();
    CONSOLE.print("USEND ");
    CONSOLE.println(n);
  } else if (strncmp(command, "urecv ", 6) == 0) {
    // urecv <n> <ms>: up to n bytes from Serial within ms, then URECV <count> <FNV-1a 32 of them, hex>.
    char *end;
    const unsigned long n = strtoul(command + 6, &end, 10);
    const unsigned long ms = strtoul(end, nullptr, 10);
    while (Serial.available()) Serial.read();
    CONSOLE.println("URECV READY");
    uint32_t hash = 2166136261u;
    unsigned long got = 0;
    const unsigned long start = millis();
    while (got < n && millis() - start < ms) {
      if (Serial.available()) {
        hash = (hash ^ (uint8_t)Serial.read()) * 16777619u;
        ++got;
      }
    }
    CONSOLE.print("URECV ");
    CONSOLE.print(got);
    CONSOLE.print(' ');
    CONSOLE.println(hash, HEX);
  } else if (strcmp(command, "uerr") == 0) {
    // uerr: UERR <mask> of what Serial.onReceiveError() reported since the last uerr (bit k: hardwareSerial_error_t k),
    // cleared. The first uerr registers the callback, so it reports 0.
    static bool registered;
    if (!registered) {
      Serial.onReceiveError([](hardwareSerial_error_t kind) { receiveErrors |= 1u << kind; });
      registered = true;
    }
    CONSOLE.print("UERR ");
    CONSOLE.println(receiveErrors);
    receiveErrors = 0;
  } else if (strncmp(command, "uhold ", 6) == 0) {
    // uhold <ms>: Serial not read for ms (after what came before is dropped), then UHOLD <available()>.
    while (Serial.available()) Serial.read();
    CONSOLE.println("UHOLD READY");
    delay(strtoul(command + 6, nullptr, 10));
    CONSOLE.print("UHOLD ");
    CONSOLE.println(Serial.available());
    while (Serial.available()) Serial.read();
  } else if (strncmp(command, "uoff ", 5) == 0) {
    // uoff <ms>: interrupts off for at least ms, so bytes coming meanwhile overrun the USART's data register. Counted
    // in loop turns (each at least 4 cycles): micros() stands still without the SysTick interrupt.
    const unsigned long ms = strtoul(command + 5, nullptr, 10);
    CONSOLE.println("UOFF READY");
    CONSOLE.flush();
    noInterrupts();
    for (volatile uint32_t i = 0; i < F_CPU / 4000u * ms; i = i + 1) {
    }
    interrupts();
    CONSOLE.println("UOFF");
    while (Serial.available()) Serial.read();
  } else if (strncmp(command, "uwrite ", 7) == 0) {
    // uwrite <n>: usend's bytes without waiting for them to go out (CTS may hold them).
    const unsigned long n = strtoul(command + 7, nullptr, 10);
    for (unsigned long i = 0; i < n; ++i) Serial.write((uint8_t)(i * 37u + 11u));
    CONSOLE.print("UWRITE ");
    CONSOLE.println(n);
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
    CONSOLE.print("UFLOW ");
    CONSOLE.println(ok ? 1 : 0);
  } else if (strncmp(command, "urts ", 5) == 0) {
    // urts <ms>: Serial not read for ms (RTS flow control holds what comes), then URTS <available()> and the count and
    // FNV-1a of everything read until 20 ms pass with nothing.
    while (Serial.available()) Serial.read();
    CONSOLE.println("URTS READY");
    delay(strtoul(command + 5, nullptr, 10));
    const int held = Serial.available();
    unsigned long count = 0;
    uint32_t hash = 2166136261u;
    drain(Serial, 20, count, hash);
    CONSOLE.print("URTS ");
    CONSOLE.print(held);
    CONSOLE.print(' ');
    CONSOLE.print(count);
    CONSOLE.print(' ');
    CONSOLE.println(hash, HEX);
  } else if (strncmp(command, "u485 ", 5) == 0) {
    // u485 <pin> | u485 off: Serial in RS485 half duplex with that pin as the driver enable, or plain UART again.
    bool ok;
    if (strcmp(command + 5, "off") == 0) {
      ok = Serial.setMode(UART_MODE_UART);
    } else {
      const int pin = (int)strtol(command + 5, nullptr, 10);
      ok = Serial.setPins(-1, -1, -1, pin) && Serial.setMode(UART_MODE_RS485_HALF_DUPLEX);
    }
    CONSOLE.print("U485 ");
    CONSOLE.println(ok ? 1 : 0);
  } else if (strncmp(command, "uirda ", 6) == 0) {
    // uirda <1 on | 0 off>: Serial in IrDA (setMode(UART_MODE_IRDA)), or plain UART again.
    const bool ok = Serial.setMode(command[6] == '1' ? UART_MODE_IRDA : UART_MODE_UART);
    CONSOLE.print("UIRDA ");
    CONSOLE.println(ok ? 1 : 0);
  } else if (strncmp(command, "ulin ", 5) == 0) {
    // ulin <id 0..63> <data hex>: setMode(UART_MODE_LIN), a frame as a master sends it - sendBreak(), the sync 0x55,
    // the protected id, the data, the enhanced checksum - ULIN <ok>; ulin -1: plain UART again.
    char *p;
    const long id = strtol(command + 5, &p, 10);
    bool ok;
    if (id < 0) {
      ok = Serial.setMode(UART_MODE_UART);
    } else {
      const uint8_t i = (uint8_t)(id & 0x3F);
      const uint8_t p0 = (i ^ i >> 1 ^ i >> 2 ^ i >> 4) & 1, p1 = ~(i >> 1 ^ i >> 3 ^ i >> 4 ^ i >> 5) & 1;
      const uint8_t pid = (uint8_t)(i | p0 << 6 | p1 << 7);
      uint16_t sum = pid;
      ok = Serial.setMode(UART_MODE_LIN) && Serial.sendBreak();
      Serial.write((uint8_t)0x55);
      Serial.write(pid);
      while (*p == ' ') {
        p++;
      }
      for (; p[0] && p[1]; p += 2) {
        char hex[3] = {p[0], p[1], 0};
        const uint8_t b = (uint8_t)strtoul(hex, nullptr, 16);
        Serial.write(b);
        sum += b;
        sum = sum > 0xFF ? sum - 0xFF : sum;
      }
      Serial.write((uint8_t)~sum);
      Serial.flush();
    }
    CONSOLE.print("ULIN ");
    CONSOLE.println(ok ? 1 : 0);
  } else if (strncmp(command, "uaddr ", 6) == 0) {
    // uaddr <0..15 | -1>: Serial.setAddress() - UADDR <ok>.
    const bool ok = Serial.setAddress((int)strtol(command + 6, nullptr, 10));
    CONSOLE.print("UADDR ");
    CONSOLE.println(ok ? 1 : 0);
  } else if (strncmp(command, "umsg ", 5) == 0) {
    // umsg <address> <n>: writeAddress(address), then n bytes of even weight - byte i is (i * 37 + 11) & 0x7F with
    // bit 7 making its count of ones even (the host reads the frames as 8E1 and 8O1). UMSG <ok> <n>.
    char *end;
    const unsigned long a = strtoul(command + 5, &end, 10);
    const unsigned long n = strtoul(end, nullptr, 10);
    const bool ok = Serial.writeAddress((uint8_t)a);
    for (unsigned long i = 0; ok && i < n; ++i) {
      uint8_t v = (uint8_t)((i * 37u + 11u) & 0x7Fu);
      if (__builtin_popcount(v) & 1) v |= 0x80u;
      Serial.write(v);
    }
    Serial.flush();
    CONSOLE.print("UMSG ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println(n);
  } else if (strncmp(command, "umute", 5) == 0) {
    // umute: Serial.mute() - UMUTE <ok>. umute w: in a message - up to 2 s for its first byte, read, then mute()
    // while the rest is still coming: UMUTE <ok> <the byte, decimal> (-1: none came).
    int first = -1;
    if (command[5] == ' ' && command[6] == 'w') {
      const unsigned long start = millis();
      while (!Serial.available() && millis() - start < 2000) {
      }
      first = Serial.read();
    }
    CONSOLE.print("UMUTE ");
    CONSOLE.print(Serial.mute() ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println(first);
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
    CONSOLE.println("UDE");
  } else if (strncmp(command, "uhd ", 4) == 0) {
    // uhd <pin>: Serial at 9600 8N1 as single-wire half duplex on that pad (RX = TX). UHD <ok> <open drain> <baud>.
    const int pin = (int)strtol(command + 4, nullptr, 10);
    Serial.end();
    Serial.begin(9600, SERIAL_8N1, pin, pin);
    CONSOLE.print("UHD ");
    CONSOLE.print(Serial ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print(CH32RV_GPIO_NO_OPEN_DRAIN ? 0 : 1);
    CONSOLE.print(' ');
    CONSOLE.println(Serial.baudRate());
  } else if (strcmp(command, "uavail") == 0) {
    // uavail: the count and FNV-1a of what Serial holds (until 20 ms with nothing), drained.
    unsigned long count = 0;
    uint32_t hash = 2166136261u;
    drain(Serial, 20, count, hash);
    CONSOLE.print("UAVAIL ");
    CONSOLE.print(count);
    CONSOLE.print(' ');
    CONSOLE.println(hash, HEX);
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
    CONSOLE.println("UEVT 1");
  } else if (strcmp(command, "uevq") == 0) {
    // uevq: UEVQ <calls> <bytes the calls read> <bytes left> <FNV-1a over both> <available at each call, comma-separated>.
    unsigned long left = 0;
    uint32_t hash = eventHash;
    drain(Serial, 5, left, hash);
    CONSOLE.print("UEVQ ");
    CONSOLE.print(eventCalls);
    CONSOLE.print(' ');
    CONSOLE.print(eventTotal);
    CONSOLE.print(' ');
    CONSOLE.print(left);
    CONSOLE.print(' ');
    CONSOLE.print(hash, HEX);
    CONSOLE.print(' ');
    for (uint8_t i = 0; i < eventCalls && i < sizeof eventSeen; ++i) {
      if (i) CONSOLE.print(',');
      CONSOLE.print(eventSeen[i]);
    }
    CONSOLE.println();
  } else if (strcmp(command, "stdio") == 0) {
    // stdout follows Serial once it is begun (setup()); ch32rv_set_stdout() overrides it, nullptr discards.
    puts("STDIO SERIAL");
    ch32rv_set_stdout(nullptr);
    puts("STDIO DISCARDED");
    ch32rv_set_stdout(&Serial);
    puts("STDIO BACK");
    fflush(stdout);
    Serial.flush();
    CONSOLE.println("STDIO DONE");
  } else {
    CONSOLE.println("ERROR COMMAND");
  }
}

void setup() {
  Serial.begin(115200);
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
