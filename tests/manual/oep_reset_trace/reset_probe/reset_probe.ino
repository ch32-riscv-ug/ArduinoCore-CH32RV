// Boot-time marker for the OEP reset trace: PA1 goes HIGH as the first thing setup() does, and
// LOW right before CH32.restart(); a debug reset drops PA1 to the P4's pull-down until setup()
// runs again. The console reports the reset reason on request.
//   REASON   -> "REASON <name>"
//   REBOOT   -> "rebooting" then PA1 LOW then CH32.restart()
#include <CH32.h>
#include "testcmd.h"

static const uint8_t MARK = PA1;

void setup() {
  pinMode(MARK, OUTPUT); digitalWrite(MARK, HIGH);   // first: the marker
  tc_begin("reset_probe");
}

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  if (!strcmp(cmd, "REASON")) { Serial.print("REASON "); Serial.println(CH32.resetReasonName()); }
  else if (!strcmp(cmd, "REBOOT")) { Serial.println("rebooting"); Serial.flush(); digitalWrite(MARK, LOW); CH32.restart(); }
  else { Serial.print("ERR "); Serial.println(cmd); }
}
