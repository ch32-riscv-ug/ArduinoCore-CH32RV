/* PIOC (tests/instrumented/pioc): CH32RVPioc running bench_pulses.S. Console on
 * CONSOLE (bench_console.h); every command answers one line:
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
 *                        <clockHz()> <cycles a bit>
 *   pnec <pin> <address> <command>
 *                        ir_nec.S (a copy of the PiocIrNec example's) on that pad, one frame as the example sends it
 *                        (address above 0xFF: extended): PNEC <begun> <cmd.out or -1> <clockHz()> <cycles a period>
 *   pin <pin> <count>    pulse_in.S (a copy of the PiocPulseIn example's) begun on that pad, then count periods
 *                        measured: PIN <begun> <clockHz()> then <high turns>:<low turns> per period (-1:-1 for one
 *                        that timed out after 200 ms); each turn is 8 cycles
 *   prx <pin> <n> <baud> uart_rx.S (a copy of the PiocUartRx example's) begun on that pad, n bytes asked for with the
 *                        waits the example works out: PRX <begun> <clockHz()> <cycles a bit>, at once (the bytes come
 *                        after)
 *   prxget               waitDone(2000): PRXGET <cmd.out or -1> <bytes taken> <them in hex>
 *   pnecrx <pin>         nec_rx.S (a copy of the PiocIrNecRx example's) begun on the pad, its ticks as the example
 *                        works them out, one command given: PNECRX <begun> <M> <a tick in ns>
 *   pnecget              waitDone(2000): PNECGET <cmd.out or -1> <d8 .. d11 as a 32-bit hex, the first bit lowest>
 *   pow <pin> <hex> <n>  onewire.S (a copy of the PiocOneWire example's) begun on the pad (left running), then as the
 *                        example: a reset, the bytes written, n bytes read:
 *                        POW <begun> <reset's cmd.out> <write's> <read's> <the bytes read, hex>
 *   ppadsall             PPADSALL <role:pin:value>... (every PIOC pad: role 0 IO0, 1 IO1; value the AF or remap)
 *   pspi <sck> <mosi> <cs> <n> <seed> <hz>
 *                        spi_tx.S (a copy of the PiocSpiTx example's) begun on the two pads once and left running: n
 *                        bytes (i * 37 + seed) with CS (a GPIO) low around them, 24 at a time, the half period from
 *                        clockHz() as the example works it out: PSPI <begun> <cmd.out or -1> <clockHz()> <half turns> */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVPioc.h>
#include "bench_console.h"
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

PIOC_IMAGE(bench_pulses);
PIOC_IMAGE(ws2812);
PIOC_IMAGE(uart_tx);
PIOC_IMAGE(ir_nec);
PIOC_IMAGE(pulse_in);
PIOC_IMAGE(uart_rx);
PIOC_IMAGE(spi_tx);
PIOC_IMAGE(nec_rx);
PIOC_IMAGE(onewire);
int uartPin = -1;            // the pad uart_tx runs on, -1: another image (or none) is begun
int necPin = -1;             // the same for ir_nec
int spiSck = -1;             // the same for spi_tx (its SCK pad)
int owPin = -1;              // the same for onewire
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
    CONSOLE.print("READY ");
    CONSOLE.print(command + 6);
    CONSOLE.print(' ');
    CONSOLE.println(BENCH_BUILD_ID);
  } else if (strcmp(command, "ppads") == 0) {
    CONSOLE.print("PPADS");
#if defined(CH32RV_PIOC_PADS)
    for (const Pad &p : pads) {
      if (p.role == 0) {
        CONSOLE.print(' ');
        CONSOLE.print(p.pin);
      }
    }
#endif
    CONSOLE.println();
  } else if (strcmp(command, "ppadsall") == 0) {
    CONSOLE.print("PPADSALL");
#if defined(CH32RV_PIOC_PADS)
    for (const Pad &p : pads) {
      CONSOLE.printf(" %u:%u:%u", p.role, p.pin, p.value);
    }
#endif
    CONSOLE.println();
  } else if (strncmp(command, "pbegin ", 7) == 0) {
    CONSOLE.print("PBEGIN ");
    uartPin = -1;
    necPin = -1;
    spiSck = -1;
    owPin = -1;
    CONSOLE.println(pioc.begin(PIOC_IMAGE_OF(bench_pulses), (int)strtol(command + 7, nullptr, 10)) ? 1 : 0);
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
    necPin = -1;
    spiSck = -1;
    owPin = -1;
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
    CONSOLE.printf("PWS %d %d %d %u %u %u %lu\r\n", begun ? 1 : 0, loaded ? 1 : 0, status, h0, h1, lo,
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
      necPin = -1;
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
    CONSOLE.printf("PUART %d %d %d %lu %lu\r\n", begun ? 1 : 0, 1, status,
                       (unsigned long)CH32RVPioc::clockHz(), (unsigned long)(3 * lo + 770 * hi + 11));
  } else if (strncmp(command, "pnec ", 5) == 0) {
    char *p;
    const int pin = (int)strtol(command + 5, &p, 10);
    const uint16_t address = (uint16_t)strtoul(p, &p, 0);
    const uint8_t cmd = (uint8_t)strtoul(p, nullptr, 0);
    auto split = [](int32_t cycles, uint8_t *out) {
      int32_t hi = cycles > 3 ? (cycles - 3) / 770 : 0;
      int32_t lo = (cycles - 770 * hi + 1) / 3;
      lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
      out[0] = (uint8_t)lo;
      out[1] = (uint8_t)hi;
    };
    /* begun once and left running, as uart_tx: a begin is a reset */
    if (necPin != pin) {
      uartPin = -1;
      necPin = pioc.begin(PIOC_IMAGE_OF(ir_nec), pin) ? pin : -1;
      delay(1);
    }
    const int32_t period = (int32_t)((CH32RVPioc::clockHz() + 19000u) / 38000u);
    uint8_t frame[8];
    frame[0] = (uint8_t)address;
    frame[1] = address > 0xFF ? (uint8_t)(address >> 8) : (uint8_t)~address;
    frame[2] = cmd;
    frame[3] = (uint8_t)~cmd;
    split(period / 3 - 11, frame + 4);
    split(period - 25 - (3 * (frame[4] ? frame[4] : 256) + 770 * frame[5]), frame + 6);
    int status = -1;
    if (necPin == pin) {
      pioc.writeData(0, frame, sizeof frame);
      pioc.command(1);
      status = pioc.waitDone(500);
    }
    const uint32_t cycles = 3u * (frame[4] ? frame[4] : 256) + 770u * frame[5] + 3u * (frame[6] ? frame[6] : 256) +
                            770u * frame[7] + 25u;
    CONSOLE.printf("PNEC %d %d %lu %lu\r\n", necPin == pin ? 1 : 0, status,
                       (unsigned long)CH32RVPioc::clockHz(), (unsigned long)cycles);
  } else if (strncmp(command, "pin ", 4) == 0) {
    char *p;
    const int pin = (int)strtol(command + 4, &p, 10);
    const unsigned long count = strtoul(p, nullptr, 10);
    uartPin = -1;
    necPin = -1;
    spiSck = -1;
    owPin = -1;
    const bool begun = pioc.begin(PIOC_IMAGE_OF(pulse_in), pin);
    CONSOLE.printf("PIN %d %lu", begun ? 1 : 0, (unsigned long)CH32RVPioc::clockHz());
    for (unsigned long k = 0; begun && k < count; k++) {
      pioc.command(1);
      if (pioc.waitDone(200) != 0) {
        pioc.begin(PIOC_IMAGE_OF(pulse_in), pin);
        CONSOLE.printf(" -1:-1");
        continue;
      }
      uint8_t c[6];
      pioc.readData(1, c, sizeof c);
      CONSOLE.printf(" %lu:%lu", (unsigned long)(c[0] | (uint32_t)c[1] << 8 | (uint32_t)c[2] << 16),
                         (unsigned long)(c[3] | (uint32_t)c[4] << 8 | (uint32_t)c[5] << 16));
    }
    CONSOLE.printf("\r\n");
  } else if (strncmp(command, "prx ", 4) == 0) {
    char *p;
    const int pin = (int)strtol(command + 4, &p, 10);
    const unsigned long n = strtoul(p, &p, 10);
    const uint32_t baud = (uint32_t)strtoul(p, nullptr, 10);
    auto split = [](int32_t cycles, uint8_t *out) {
      int32_t hi = cycles > 3 ? (cycles - 3) / 770 : 0;
      int32_t lo = (cycles - 770 * hi + 1) / 3;
      lo = lo < 1 ? 1 : lo > 256 ? 256 : lo;
      out[0] = (uint8_t)lo;
      out[1] = (uint8_t)hi;
    };
    uartPin = -1;
    necPin = -1;
    spiSck = -1;
    owPin = -1;
    const bool begun = pioc.begin(PIOC_IMAGE_OF(uart_rx), pin);
    const int32_t bit = (int32_t)((CH32RVPioc::clockHz() + baud / 2) / baud);
    uint8_t params[5] = {(uint8_t)(n > 24 ? 24 : n)};
    split(bit - 11, params + 1);
    split(bit + bit / 2 - 12, params + 3);
    if (begun) {
      pioc.writeData(0, params, sizeof params);
      pioc.command(1);
    }
    CONSOLE.printf("PRX %d %lu %lu\r\n", begun ? 1 : 0, (unsigned long)CH32RVPioc::clockHz(),
                       (unsigned long)(3u * (params[1] ? params[1] : 256) + 770u * params[2] + 11u));
  } else if (strcmp(command, "prxget") == 0) {
    const int status = pioc.waitDone(2000);
    const uint8_t got = pioc.read(7);
    uint8_t bytes[24];
    pioc.readData(8, bytes, got > 24 ? 24 : got);
    CONSOLE.printf("PRXGET %d %u ", status, got);
    for (uint8_t i = 0; i < got && i < 24; i++) {
      CONSOLE.printf("%02x", bytes[i]);
    }
    CONSOLE.printf("\r\n");
  } else if (strncmp(command, "pcmd ", 5) == 0) {
    char *p;
    const unsigned long n = strtoul(command + 5, &p, 10);
    const unsigned long delayTurns = strtoul(p, nullptr, 10);
    pioc.write(0, (uint8_t)delayTurns);
    pioc.command((uint8_t)n);
    const int status = pioc.waitDone(1000);
    CONSOLE.print("PCMD ");
    CONSOLE.print(status);
    CONSOLE.print(' ');
    CONSOLE.print(pioc.read(1));
    CONSOLE.print(' ');
    CONSOLE.println(pioc.done() ? 1 : 0);   // the request must be down once its cmd.out is taken
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
    CONSOLE.print("PIRQ ");
    CONSOLE.print(irqStatus);
    CONSOLE.print(' ');
    CONSOLE.print(irqCalls);
    CONSOLE.print(' ');
    CONSOLE.println(pioc.read(1));
  } else if (strncmp(command, "pow ", 4) == 0) {
    char *p;
    const int pin = (int)strtol(command + 4, &p, 10);
    while (*p == ' ') {
      p++;
    }
    char *count = strchr(p, ' ');
    if (count) {
      *count++ = '\0';
    }
    const uint8_t n = count ? (uint8_t)strtoul(count, nullptr, 10) : 0;
    uint8_t out[24], in[24];
    uint8_t k = 0;
    for (; p[0] && p[1] && k < sizeof out; p += 2) {
      char pair[3] = {p[0], p[1], 0};
      out[k++] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    bool begun = owPin == pin;
    if (!begun) {
      uartPin = -1;
      necPin = -1;
      spiSck = -1;
      begun = pioc.begin(PIOC_IMAGE_OF(onewire), pin) && pioc.setOpenDrain(0);
      owPin = begun ? pin : -1;
    }
    int32_t turns = (int32_t)((CH32RVPioc::clockHz() / 1000000 - 4 + 1) / 3);
    const uint8_t us = (uint8_t)(turns < 1 ? 1 : turns > 255 ? 255 : turns);
    auto run = [&](uint8_t cmd, uint8_t c) {
      pioc.write(0, c);
      pioc.write(1, us);
      pioc.command(cmd);
      return pioc.waitDone(100);
    };
    int reset = -1, wrote = -1, read = -1;
    if (begun) {
      reset = run(1, 0);
      pioc.writeData(8, out, k);
      wrote = k ? run(2, k) : 0;
      read = n ? run(3, n > 24 ? 24 : n) : 0;
      pioc.readData(8, in, n > 24 ? 24 : n);
    }
    CONSOLE.printf("POW %d %d %d %d ", begun ? 1 : 0, reset, wrote, read);
    for (uint8_t i = 0; i < n && i < 24; i++) {
      CONSOLE.printf("%02x", in[i]);
    }
    CONSOLE.printf("\r\n");
  } else if (strncmp(command, "pnecrx ", 7) == 0) {
    const int pin = (int)strtol(command + 7, nullptr, 10);
    uartPin = -1;
    necPin = -1;
    spiSck = -1;
    owPin = -1;
    const bool begun = pioc.begin(PIOC_IMAGE_OF(nec_rx), pin);
    const uint32_t clk = CH32RVPioc::clockHz();
    uint32_t m = (uint32_t)((uint64_t)clk * 75 / 1000000 / 773);
    m = m < 1 ? 1 : m > 255 ? 255 : m;
    const float tickUs = (float)(m * 773 + 14) * 1e6f / (float)clk;
    const uint8_t ticks[4] = {(uint8_t)m, (uint8_t)(256 - (int)(1125.0f / tickUs + 0.5f)),
                              (uint8_t)(256 - (int)(3375.0f / tickUs + 0.5f)),
                              (uint8_t)(256 - (int)(6000.0f / tickUs + 0.5f))};
    if (begun) {
      pioc.writeData(1, ticks, sizeof ticks);
      pioc.command(1);
    }
    CONSOLE.printf("PNECRX %d %lu %lu\r\n", begun ? 1 : 0, (unsigned long)m, (unsigned long)(tickUs * 1000.0f));
  } else if (strcmp(command, "pnecget") == 0) {
    const int status = pioc.waitDone(2000);
    uint8_t b[4];
    pioc.readData(8, b, sizeof b);
    CONSOLE.printf("PNECGET %d %08lx\r\n", status,
                       (unsigned long)(b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24));
  } else if (strncmp(command, "pspi ", 5) == 0) {
    char *p;
    const int sck = (int)strtol(command + 5, &p, 10);
    const int mosi = (int)strtol(p, &p, 10);
    const int cs = (int)strtol(p, &p, 10);
    const unsigned long n = strtoul(p, &p, 10);
    const unsigned long seed = strtoul(p, &p, 10);
    const uint32_t hz = (uint32_t)strtoul(p, nullptr, 10);
    const int32_t half = (int32_t)((CH32RVPioc::clockHz() + hz) / (2 * hz));
    int32_t turns = (half - 6 + 1) / 3;
    turns = turns < 1 ? 1 : turns > 256 ? 256 : turns;
    const uint8_t h = (uint8_t)turns;
    bool begun = spiSck == sck;
    if (!begun) {
      uartPin = -1;
      necPin = -1;
      begun = pioc.begin(PIOC_IMAGE_OF(spi_tx), sck, mosi);
      spiSck = begun ? sck : -1;
      pinMode(cs, OUTPUT);
      digitalWrite(cs, HIGH);
      delay(1);
    }
    int status = begun ? 0 : -1;
    uint8_t chunk[24];
    digitalWrite(cs, LOW);
    for (unsigned long i = 0; status == 0 && i < n; i += sizeof chunk) {
      const uint8_t k = (uint8_t)(n - i > sizeof chunk ? sizeof chunk : n - i);
      for (uint8_t j = 0; j < k; j++) {
        chunk[j] = (uint8_t)((i + j) * 37u + seed);
      }
      pioc.writeData(8, chunk, k);
      pioc.writeData(1, &h, 1);
      pioc.write(0, k);
      pioc.command(1);
      status = pioc.waitDone(2000);
    }
    digitalWrite(cs, HIGH);
    CONSOLE.printf("PSPI %d %d %lu %d\r\n", begun ? 1 : 0, status, (unsigned long)CH32RVPioc::clockHz(), (int)turns);
  } else if (strcmp(command, "pend") == 0) {
    spiSck = -1;
    uartPin = -1;
    necPin = -1;
    spiSck = -1;
    owPin = -1;
    pioc.end();
    CONSOLE.println("PEND");
  } else {
    CONSOLE.println("ERROR COMMAND");
  }
}

void setup() {
  CONSOLE_BEGIN(115200);
  CONSOLE.print("STARTUP ");
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
