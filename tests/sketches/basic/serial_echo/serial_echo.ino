// Serial receive, which serial_println cannot cover: it only proves TX.
// The host sends lines, the target answers, so a pass means both directions of
// the UART and the RX interrupt path work.
//
// Needs the probe's UART TX wired to the board's Serial RX pin as well as the
// usual TX. The uart_scan manual test only finds TX, so RX is what this adds.
//
// This is the one sketch with no RUN: receiving *is* the thing under test, so
// its vocabulary is the test. See tests/TEST_PLAN.ja.md.
//
//   ECHO <text>   ->  echo:<text>
//   LEN <text>    ->  len=<count>
//
// PING/PONG comes from the template and is a receive test in its own right,
// which is why the host's handshake already proves half of this.
#include "testcmd.h"

/* Argument of "<verb> <text>", or NULL when the verb does not match.
 * Pointer arithmetic into tc_ready()'s buffer: no copy, no allocation. */
static const char *argument(const char *cmd, const char *verb)
{
  const size_t n = strlen(verb);
  if (strncmp(cmd, verb, n) != 0 || cmd[n] != ' ') {
    return NULL;
  }
  return cmd + n + 1;
}

void setup()
{
  tc_begin("serial_echo");
}

void loop()
{
  const char *cmd = tc_ready();
  if (!cmd) {
    return;
  }

  const char *text = argument(cmd, "ECHO");
  if (text) {
    Serial.print("echo:");
    Serial.println(text);
    return;
  }

  text = argument(cmd, "LEN");
  if (text) {
    Serial.print("len=");
    Serial.println((unsigned)strlen(text));
    return;
  }

  tc_unknown(cmd);
}
