/* PIOC (tests/instrumented/pioc): CH32RVPioc running bench_pulses.S. Console on
 * SerialDMSeq; every command answers one line:
 *
 *   ready <nonce>        READY <nonce> <build id>
 *   ppads                PPADS <the IO0 pads the PIOC reaches, pin numbers> (PPADS alone: no PIOC)
 *   pbegin <pin>         begin() with IO0 on that pad: PBEGIN <1 / 0>
 *   pcmd <n> <delay>     d0 = delay, command(n), waitDone(1000): PCMD <cmd.out or -1> <d1> <done() after: 1 / 0>
 *   pirq <n> <delay>     the same with onDone(): PIRQ <the callback's cmd.out or -1> <calls> <d1>
 *   pend                 end(): PEND
 *   pws <pin> <n> <seed> ws2812.S (a copy of the PiocWs2812 example's) with IO0 on that pad: n bytes
 *                        (byte i = i * 37 + seed) by loadSram() at 0x400, the timing from clockHz() as the
 *                        example works it out, command, waitDone(): PWS <begun 1/0> <loaded 1/0> <cmd.out or -1>
 *                        <d2 d3 d4: the turns> <microseconds the command took>; bench_pulses is begun again after
 *   puart <pin> <n> <seed> <baud>
 *                        uart_tx.S (a copy of the PiocUartTx example's), begun on that pad once and left running
 *                        (another command that begins an image ends it): n bytes (i * 37 + seed) at baud, 24 at a
 *                        time through d8 .. d31 as the example sends them: PUART <begun> 1 <cmd.out or -1>
 *                        <clockHz()> <cycles a bit> */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVPioc.h>
#include <SerialDMSeq.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

PIOC_IMAGE(bench_pulses);
PIOC_IMAGE(ws2812);
PIOC_IMAGE(uart_tx);
int uartPin = -1;            // the pad uart_tx runs on, -1: another image (or none) is begun
CH32RVPioc pioc;
volatile int irqStatus = -1;
volatile uint8_t irqCalls;
char line[64];
uint8_t used;

#if defined(CH32RV_PIOC_PADS)
struct Pad {
  uint8_t role, pin, value;
};
static const Pad pads[] = CH32RV_PIOC_PADS;
#endif

void reply(char *command) {
  if (strncmp(command, "ready ", 6) == 0) {
    SerialDMSeq.print("READY ");
    SerialDMSeq.print(command + 6);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(BENCH_BUILD_ID);
  } else if (strcmp(command, "ppads") == 0) {
    SerialDMSeq.print("PPADS");
#if defined(CH32RV_PIOC_PADS)
    for (const Pad &p : pads) {
      if (p.role == 0) {
        SerialDMSeq.print(' ');
        SerialDMSeq.print(p.pin);
      }
    }
#endif
    SerialDMSeq.println();
  } else if (strncmp(command, "pbegin ", 7) == 0) {
    SerialDMSeq.print("PBEGIN ");
    uartPin = -1;
    SerialDMSeq.println(pioc.begin(PIOC_IMAGE_OF(bench_pulses), (int)strtol(command + 7, nullptr, 10)) ? 1 : 0);
  } else if (strncmp(command, "pws ", 4) == 0) {
    char *p;
    const int pin = (int)strtol(command + 4, &p, 10);
    const unsigned long n = strtoul(p, &p, 10);
    const unsigned long seed = strtoul(p, nullptr, 10);
    static uint8_t bytes[1024];
    const size_t count = n > sizeof bytes ? sizeof bytes : n;
    for (size_t i = 0; i < count; i++) {
      bytes[i] = (uint8_t)(i * 37u + seed);
    }
    uartPin = -1;
    const bool begun = pioc.begin(PIOC_IMAGE_OF(ws2812), pin);
    const bool loaded = begun && pioc.loadSram(0x400, bytes, count);
    auto turns = [](uint32_t ns, uint32_t fixed) -> uint8_t {
      const uint32_t cycles = (uint32_t)((uint64_t)ns * CH32RVPioc::clockHz() / 1000000000u);
      const int32_t t = ((int32_t)cycles - (int32_t)fixed + 1) / 3;
      return (uint8_t)(t < 1 ? 1 : t > 255 ? 255 : t);
    };
    const uint8_t h0 = turns(350, 4), h1 = turns(800, 6 + 3 * h0), lo = turns(1250, 13 + 3 * (h0 + h1));
    const uint8_t last = lo > 6 ? lo - 5 : 1;
    const uint8_t params[6] = {(uint8_t)count, (uint8_t)(count >> 8), h0, h1, lo, last};
    int status = -1;
    uint32_t took = 0;
    if (loaded) {
      pioc.writeData(0, params, sizeof params);
      const uint32_t t0 = micros();
      pioc.command(1);
      status = pioc.waitDone(1000);
      took = micros() - t0;
    }
    delayMicroseconds(300);
    SerialDMSeq.printf("PWS %d %d %d %u %u %u %lu\r\n", begun ? 1 : 0, loaded ? 1 : 0, status, h0, h1, lo,
                       (unsigned long)took);
    pioc.begin(PIOC_IMAGE_OF(bench_pulses), pin);
  } else if (strncmp(command, "puart ", 6) == 0) {
    char *p;
    const int pin = (int)strtol(command + 6, &p, 10);
    const unsigned long n = strtoul(p, &p, 10);
    const unsigned long seed = strtoul(p, &p, 10);
    const uint32_t baud = (uint32_t)strtoul(p, nullptr, 10);
    const uint32_t cycles = (CH32RVPioc::clockHz() + baud / 2) / baud;
    const uint32_t hi = cycles > 11 + 3 ? (cycles - 11 - 3) / 770 : 0;
    uint32_t lo = (cycles - 11 - 770 * hi + 1) / 3;
    lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
    /* begun once and left running: a begin (or loadSram) holds the PIOC in reset, the pad floating */
    bool begun = uartPin == pin;
    if (!begun) {
      begun = pioc.begin(PIOC_IMAGE_OF(uart_tx), pin);
      uartPin = begun ? pin : -1;
      delay(1);
    }
    int status = begun ? 0 : -1;
    uint8_t chunk[24];
    for (unsigned long i = 0; status == 0 && i < n; i += sizeof chunk) {
      const uint8_t k = (uint8_t)(n - i > sizeof chunk ? sizeof chunk : n - i);
      for (uint8_t j = 0; j < k; j++) {
        chunk[j] = (uint8_t)((i + j) * 37u + seed);
      }
      const uint8_t timing[2] = {(uint8_t)lo, (uint8_t)hi};
      pioc.writeData(8, chunk, k);
      pioc.writeData(1, timing, sizeof timing);
      pioc.write(0, k);
      pioc.command(1);
      status = pioc.waitDone(2000);
    }
    SerialDMSeq.printf("PUART %d %d %d %lu %lu\r\n", begun ? 1 : 0, 1, status,
                       (unsigned long)CH32RVPioc::clockHz(), (unsigned long)(3 * lo + 770 * hi + 11));
  } else if (strncmp(command, "pcmd ", 5) == 0) {
    char *p;
    const unsigned long n = strtoul(command + 5, &p, 10);
    const unsigned long delayTurns = strtoul(p, nullptr, 10);
    pioc.write(0, (uint8_t)delayTurns);
    pioc.command((uint8_t)n);
    const int status = pioc.waitDone(1000);
    SerialDMSeq.print("PCMD ");
    SerialDMSeq.print(status);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(pioc.read(1));
    SerialDMSeq.print(' ');
    SerialDMSeq.println(pioc.done() ? 1 : 0);   // the request must be down once its cmd.out is taken
  } else if (strncmp(command, "pirq ", 5) == 0) {
    char *p;
    const unsigned long n = strtoul(command + 5, &p, 10);
    const unsigned long delayTurns = strtoul(p, nullptr, 10);
    irqStatus = -1;
    irqCalls = 0;
    pioc.onDone([](uint8_t status) {
      irqStatus = status;
      irqCalls = irqCalls + 1;
    });
    pioc.write(0, (uint8_t)delayTurns);
    pioc.command((uint8_t)n);
    const unsigned long start = millis();
    while (!irqCalls && millis() - start < 1000) {
    }
    delay(5);                                  // a second call would show
    pioc.onDone(nullptr);
    SerialDMSeq.print("PIRQ ");
    SerialDMSeq.print(irqStatus);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(irqCalls);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(pioc.read(1));
  } else if (strcmp(command, "pend") == 0) {
    uartPin = -1;
    pioc.end();
    SerialDMSeq.println("PEND");
  } else {
    SerialDMSeq.println("ERROR COMMAND");
  }
}

void setup() {
  SerialDMSeq.begin(115200);
  SerialDMSeq.print("STARTUP ");
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
