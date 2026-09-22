// GPIO / pull / EXTI on request, one pin at a time, for the OEP fixture.gpio matrix (worklist P3 row 1).
//   MODE <pin> <m>     m: 0 INPUT, 1 INPUT_PULLUP, 2 INPUT_PULLDOWN, 3 OUTPUT, 4 OUTPUT_OPENDRAIN  -> "MODE ok"
//   WRITE <pin> <v>    digitalWrite                                                             -> "WRITE ok"
//   READ <pin>         digitalRead                                                              -> "READ v=<0|1>"
//   EXTI <pin> <mode>  attachInterrupt, mode 0 RISING / 1 FALLING / 2 CHANGE, counter reset       -> "EXTI armed"
//   COUNT              -> "COUNT n=<edges>"
//   EXTIOFF <pin>      detachInterrupt                                                          -> "EXTIOFF ok"
// Pins are named (PA0 ...). Only the E143-wired pads are in the table.
#define TC_CMD_MAX 64
#include "testcmd.h"

struct Named { const char *name; uint8_t pin; };
static const Named kPins[] = {{"PA0", PA0}, {"PA1", PA1}, {"PA2", PA2}, {"PA3", PA3}, {"PA4", PA4}, {"PA5", PA5}, {"PA6", PA6}, {"PA7", PA7},
                              {"PB3", PB3}, {"PB11", PB11}, {"PB12", PB12}, {"PC14", PC14}, {"PC15", PC15}, {"PB0", PB0}, {"PB1", PB1}};
static volatile uint32_t gEdges = 0;
static void onEdge() { gEdges++; }

static int lookup(const char *name) {
  for (const Named &n : kPins) if (!strcmp(n.name, name)) return n.pin;
  return -1;
}

void setup() { tc_begin("gpio_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}, pinName[6] = {0}; unsigned a = 0;
  const int n = sscanf(cmd, "%7s %5s %u", verb, pinName, &a);
  if (n < 1) return;
  if (!strcmp(verb, "COUNT")) { Serial.print("COUNT n="); Serial.println(gEdges); return; }
  const int pin = lookup(pinName);
  if (pin < 0) { Serial.print("ERR pin "); Serial.println(pinName); return; }
  if (!strcmp(verb, "MODE")) {
    static const PinMode modes[] = {INPUT, INPUT_PULLUP, INPUT_PULLDOWN, OUTPUT, OUTPUT_OPENDRAIN};
    if (a > 4) { Serial.println("ERR mode"); return; }
    pinMode((pin_size_t)pin, modes[a]); Serial.println("MODE ok");
  } else if (!strcmp(verb, "WRITE")) { digitalWrite((pin_size_t)pin, a ? HIGH : LOW); Serial.println("WRITE ok"); }
  else if (!strcmp(verb, "READ")) { Serial.print("READ v="); Serial.println(digitalRead((pin_size_t)pin) ? 1 : 0); }
  else if (!strcmp(verb, "EXTI")) {
    static const PinStatus modes[] = {RISING, FALLING, CHANGE};
    if (a > 2) { Serial.println("ERR mode"); return; }
    gEdges = 0; attachInterrupt(digitalPinToInterrupt(pin), onEdge, modes[a]); Serial.println("EXTI armed");
  } else if (!strcmp(verb, "EXTIOFF")) { detachInterrupt(digitalPinToInterrupt(pin)); Serial.println("EXTIOFF ok"); }
  else { Serial.print("ERR verb "); Serial.println(verb); }
}
