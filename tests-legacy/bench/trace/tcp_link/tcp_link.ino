// The DUT side of tests-legacy/bench/trace/tcp_link: nothing but the console (READY, PING -> PONG), so the test can tell an
// upload and a monitor made over the probe's TCP link from the sketch that was there before.
#include <CH32RV.h>
#include "testcmd.h"

void setup() {
  tc_begin("tcp_link");
}

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  Console.print("ERR "); Console.println(cmd);
}
