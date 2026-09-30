// GPIO / pull / EXTI on request, one pin at a time, for the OEP fixture.gpio matrix (tests/bench/trace/gpio_probe).
//   MODE <pin> <m>     m: 0 INPUT, 1 INPUT_PULLUP, 2 INPUT_PULLDOWN, 3 OUTPUT, 4 OUTPUT_OPENDRAIN  -> "MODE ok"
//   WRITE <pin> <v>    digitalWrite                                                             -> "WRITE ok"
//   READ <pin>         digitalRead                                                              -> "READ v=<0|1>"
//   EXTI <pin> <mode>  attachInterrupt, mode 0 RISING / 1 FALLING / 2 CHANGE, counter reset       -> "EXTI armed"
//   COUNT              -> "COUNT n=<edges>"
//   EXTIOFF <pin>      detachInterrupt                                                          -> "EXTIOFF ok"
// <pin> is the core's pin number ((port << 5) | bit); the host derives it from the pad names in the bench file,
// so no jig's table is baked in here.
#define TC_CMD_MAX 64
#include "testcmd.h"

static volatile uint32_t gEdges = 0;
static void onEdge() { gEdges++; }

void setup() { tc_begin("gpio_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned pin = 0, a = 0;
  const int n = sscanf(cmd, "%7s %u %u", verb, &pin, &a);
  if (n < 1) return;
  if (!strcmp(verb, "COUNT")) { Console.print("COUNT n="); Console.println(gEdges); return; }
  if (n < 2 || !digitalPinIsValid((pin_size_t)pin)) { Console.print("ERR pin "); Console.println(pin); return; }
  if (!strcmp(verb, "MODE")) {
    static const PinMode modes[] = {INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT, OUTPUT_OPENDRAIN};
    if (a > 4) { Console.println("ERR mode"); return; }
    pinMode((pin_size_t)pin, modes[a]); Console.println("MODE ok");
  } else if (!strcmp(verb, "WRITE")) { digitalWrite((pin_size_t)pin, a ? HIGH : LOW); Console.println("WRITE ok"); }
  else if (!strcmp(verb, "READ")) { Console.print("READ v="); Console.println(digitalRead((pin_size_t)pin) ? 1 : 0); }
  else if (!strcmp(verb, "EXTI")) {
    static const PinStatus modes[] = {RISING, FALLING, CHANGE};
    if (a > 2) { Console.println("ERR mode"); return; }
    gEdges = 0; attachInterrupt(digitalPinToInterrupt(pin), onEdge, modes[a]); Console.println("EXTI armed");
  } else if (!strcmp(verb, "EXTIOFF")) { detachInterrupt(digitalPinToInterrupt(pin)); Console.println("EXTIOFF ok"); }
  else { Console.print("ERR verb "); Console.println(verb); }
}
