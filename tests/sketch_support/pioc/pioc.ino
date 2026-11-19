/* PIOC (tests/instrumented/pioc): CH32RVPioc running bench_pulses.S. Console on
 * SerialDMSeq; every command answers one line:
 *
 *   ready <nonce>        READY <nonce> <build id>
 *   ppads                PPADS <the IO0 pads the PIOC reaches, pin numbers> (PPADS alone: no PIOC)
 *   pbegin <pin>         begin() with IO0 on that pad: PBEGIN <1 / 0>
 *   pcmd <n> <delay>     d0 = delay, command(n), waitDone(1000): PCMD <cmd.out or -1> <d1>
 *   pend                 end(): PEND */
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
CH32RVPioc pioc;
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
    SerialDMSeq.println(pioc.begin(PIOC_IMAGE_OF(bench_pulses), (int)strtol(command + 7, nullptr, 10)) ? 1 : 0);
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
    SerialDMSeq.println(pioc.read(1));
  } else if (strcmp(command, "pend") == 0) {
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
