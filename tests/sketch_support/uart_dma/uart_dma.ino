/* UART DMA contracts (tests/instrumented/uart/test_uart_dma.py): Serial with the buffers of setRxBufferSize() /
 * setTxBufferSize(), moved by DMA, on the probe's UART. Console on SerialDMSeq; every command answers one line:
 *
 *   dmax                      DMAX <the highest baud Serial's USART takes: its clock / 16>
 *   dbegin <baud> <rx> <tx>   Serial again with those buffer sizes (0: the built-in path) at that baud, 8N1:
 *                             DBEGIN <rx size set> <tx size set> <open> <DMAR> <DMAT> (the USART's CTLR3: the DMA in use)
 *   drecv <n> <ms>            up to n bytes within ms: DRECV <count> <FNV-1a> <errors>
 *   doff <ms> <n>             interrupts off for at least ms, then DOFF <available() still with interrupts off> and the
 *                             count, FNV-1a and errors of up to n bytes read until 20 ms with nothing
 *   dsend <n> <off>           n bytes ((i * 37 + 11) & 0xFF) and flush(): DSEND <n> <microseconds>; off 1: the writes
 *                             with interrupts off (the DMA's run moved on by write() itself)
 *   devt <only> <symbols> <fifo> | devt off, devq   onReceive() on the DMA buffer, as uart.ino's uevt / uevq
 *
 * <errors>: Serial.takeErrors() - STATR's ORE 8 / NE 4 / FE 2 / PE 1, 16 break, 32 buffer full. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include "internal/ch32rv_registers.h"   // the USART's CTLR3: DMAR / DMAT show the DMA in use
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

uint32_t requests;
char line[80];
uint8_t used;

uint8_t eventCalls;
uint8_t eventSeen[8];
uint32_t eventTotal;
uint32_t eventHash = 2166136261u;

// FNV-1a's multiply by 16777619 (2^24 + 2^8 + 0x93) in shifts: rv32ec (CH32V003) has no multiply, and __mulsi3 per
// byte left the reader below 1 Mbaud (bench 2026-11-17).
uint32_t fnv(uint32_t hash, uint8_t byte) {
  const uint32_t h = hash ^ byte;
  return h + (h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24);
}

// The registers of Serial's USART (0 while closed).
uint32_t serialBase() {
  switch (Serial.peripheral()) {
    case 1: return CH32RV_USART1_BASE;
#ifdef CH32RV_SERIAL2_CLKEN_ADDR
    case 2: return CH32RV_USART2_BASE;
#endif
#ifdef CH32RV_SERIAL3_CLKEN_ADDR
    case 3: return CH32RV_USART3_BASE;
#endif
#ifdef CH32RV_SERIAL4_CLKEN_ADDR
    case 4: return CH32RV_USART4_BASE;
#endif
    default: return 0;
  }
}

// Every byte until quietMs pass with none (or n of them): count and FNV-1a.
void drain(unsigned long n, unsigned long quietMs, unsigned long &count, uint32_t &hash) {
  unsigned long last = millis();
  while (count < n && millis() - last < quietMs) {
    if (Serial.available()) {
      hash = fnv(hash, (uint8_t)Serial.read());
      ++count;
      last = millis();
    }
  }
}

// Interrupts off for at least ms: counted in loop turns (each at least 4 cycles), micros() stands still meanwhile.
void interruptsOff(unsigned long ms) {
  for (volatile uint32_t i = 0; i < F_CPU / 4000u * ms; i = i + 1) {
  }
}

void reply(const char *command) {
  ++requests;
  char *end;
  if (strncmp(command, "ready ", 6) == 0) {
    SerialDMSeq.print("READY ");
    SerialDMSeq.print(command + 6);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(BENCH_BUILD_ID);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(requests);
  } else if (strcmp(command, "dmax") == 0) {
    const uint32_t clock = Serial.peripheral() == 1 ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
    SerialDMSeq.print("DMAX ");
    SerialDMSeq.println(clock / 16u);
  } else if (strncmp(command, "dbegin ", 7) == 0) {
    const unsigned long baud = strtoul(command + 7, &end, 10);
    const unsigned long rx = strtoul(end, &end, 10);
    const unsigned long tx = strtoul(end, nullptr, 10);
    Serial.end();
    const size_t r = rx ? Serial.setRxBufferSize(rx) : 0;
    const size_t t = tx ? Serial.setTxBufferSize(tx) : 0;
    Serial.begin(baud);
    const uint32_t base = serialBase();
    const uint16_t ctlr3 = base ? CH32RV_USART_CTLR3(base) : 0;
    SerialDMSeq.print("DBEGIN ");
    SerialDMSeq.print(r);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(t);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(Serial ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(ctlr3 & CH32RV_USART_CTLR3_DMAR ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(ctlr3 & CH32RV_USART_CTLR3_DMAT ? 1 : 0);
  } else if (strncmp(command, "drecv ", 6) == 0) {
    const unsigned long n = strtoul(command + 6, &end, 10);
    const unsigned long ms = strtoul(end, nullptr, 10);
    while (Serial.available()) Serial.read();
    Serial.takeErrors();
    SerialDMSeq.println("DRECV READY");
    uint32_t hash = 2166136261u;
    unsigned long got = 0;
    const unsigned long start = millis();
    while (got < n && millis() - start < ms) {
      if (Serial.available()) {
        hash = fnv(hash, (uint8_t)Serial.read());
        ++got;
      }
    }
    SerialDMSeq.print("DRECV ");
    SerialDMSeq.print(got);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(hash, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(Serial.takeErrors());
  } else if (strncmp(command, "doff ", 5) == 0) {
    const unsigned long ms = strtoul(command + 5, &end, 10);
    const unsigned long n = strtoul(end, nullptr, 10);
    while (Serial.available()) Serial.read();
    Serial.takeErrors();
    SerialDMSeq.println("DOFF READY");
    SerialDMSeq.flush();
    noInterrupts();
    interruptsOff(ms);
    const int held = Serial.available();   // the DMA's count, read with interrupts still off
    interrupts();
    unsigned long count = 0;
    uint32_t hash = 2166136261u;
    drain(n, 20, count, hash);
    SerialDMSeq.print("DOFF ");
    SerialDMSeq.print(held);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(count);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(hash, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(Serial.takeErrors());
  } else if (strncmp(command, "dsend ", 6) == 0) {
    const unsigned long n = strtoul(command + 6, &end, 10);
    const bool off = strtoul(end, nullptr, 10) != 0;
    const uint32_t start = micros();
    uint8_t b = 11;                        // (i * 37 + 11) & 0xFF without a multiply per byte (rv32ec)
    if (off) noInterrupts();
    for (unsigned long i = 0; i < n; ++i, b += 37) Serial.write(b);
    if (off) interrupts();
    Serial.flush();
    const uint32_t took = micros() - start;
    SerialDMSeq.print("DSEND ");
    SerialDMSeq.print(n);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(took);
  } else if (strncmp(command, "devt ", 5) == 0) {
    eventCalls = 0;
    eventTotal = 0;
    eventHash = 2166136261u;
    while (Serial.available()) Serial.read();
    if (strcmp(command + 5, "off") == 0) {
      Serial.onReceive(nullptr);
    } else {
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
    SerialDMSeq.println("DEVT 1");
  } else if (strcmp(command, "devq") == 0) {
    unsigned long left = 0;
    uint32_t hash = eventHash;
    drain(0xFFFFFFFFul, 5, left, hash);
    SerialDMSeq.print("DEVQ ");
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
