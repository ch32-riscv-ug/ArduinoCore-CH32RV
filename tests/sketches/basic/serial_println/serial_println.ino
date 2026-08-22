// Milestone 1 acceptance sketch: Serial must come up and print on every target board.
// Keep this sketch free of anything beyond Serial - it is the gate for "Serial works",
// not a peripheral test.
//
// The RUN command is what makes it a gate rather than a coin toss: setup() only
// announces itself, so the lines below are printed after the host has proved
// the link with PING/PONG and cannot be confused with the last sketch's output.
// See testcmd.h.
#include "testcmd.h"

static void run_checks()
{
  Serial.println("hello from ch32");
  Serial.print("int=");
  Serial.println(42);
  Serial.print("hex=");
  Serial.println(0xBEEF, HEX);
  tc_done();
}

void setup()
{
  tc_begin("serial_println");
}

void loop()
{
  const char *cmd = tc_ready();
  if (!cmd) {
    return;
  }
  if (!strcmp(cmd, "RUN")) {
    run_checks();
  } else {
    tc_unknown(cmd);
  }
}
