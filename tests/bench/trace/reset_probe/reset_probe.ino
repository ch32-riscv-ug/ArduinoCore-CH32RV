// Boot-time marker for tests/bench/trace/reset_probe: the marker pad goes HIGH as the first thing setup()
// does, and LOW right before CH32.restart(); a debug reset drops it to the probe's pull-down until setup()
// runs again. The console reports the reset reason on request.
//   REASON   -> "REASON <name>"
//   REBOOT   -> "rebooting" then marker LOW then CH32.restart()
// The marker has to be set before anything else runs, so it is a compile-time value: BENCH_MARK_PIN, the
// core's pin number as a string, from build_config.toml <- TEST_BENCH_MARK_PIN, which bench/conftest.py
// sets from the bench file's facts.pwm. Without it, PA1.
#include <CH32.h>
#include <stdlib.h>
#include "testcmd.h"

#ifdef BENCH_MARK_PIN
static const uint8_t MARK = (uint8_t)atoi(BENCH_MARK_PIN);
#else
static const uint8_t MARK = PA1;
#endif

void setup() {
  pinMode(MARK, OUTPUT); digitalWrite(MARK, HIGH);   // first: the marker
  tc_begin("reset_probe");
}

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  if (!strcmp(cmd, "REASON")) { Console.print("REASON "); Console.println(CH32.resetReasonName()); }
  else if (!strcmp(cmd, "MARK")) { Console.print("MARK "); Console.println(MARK); }
  else if (!strcmp(cmd, "REBOOT")) { Console.println("rebooting"); Console.flush(); digitalWrite(MARK, LOW); CH32.restart(); }
  else { Console.print("ERR "); Console.println(cmd); }
}
