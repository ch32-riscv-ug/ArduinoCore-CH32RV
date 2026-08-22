/* Print's number formatting: what every sketch depends on, and where a core
 * most often differs from Arduino in a way nothing else notices.
 *
 * Split out of core_api because of what one line costs. `Serial.println(1.5, 2)`
 * is 9428 bytes on CH32V003: Print::printFloat takes a double, rv32ec has no
 * FPU, and the soft-float routines come in behind it - __adddf3 2346,
 * __subdf3 2252, __divdf3 1818, __muldf3 1510, printFloat itself 468, plus
 * __clz_tab and the comparisons. core_api was at 97% of a 16 KB part with that
 * line and is at 39% without it.
 *
 * printFloat's signature is ArduinoCore-API's, vendored unmodified (ADR-0009),
 * so the cost is not ours to remove - only to keep in the one sketch that is
 * about formatting rather than in the big one.
 *
 * The expected strings below come from reading cores/arduino/api/Print.cpp,
 * not from what a board happened to print: printFloat adds 0.5 scaled to the
 * digit count and then peels digits off the remainder, and prints no decimal
 * point at all when asked for zero digits. A part that disagrees is a finding.
 */
#include "testcmd.h"

static void run_checks()
{
  /* Uppercase hex with no prefix, a negative decimal, and two decimals -
   * the three a sketch is most likely to depend on. */
  Serial.print("fmt=");
  Serial.print(255, HEX);
  Serial.print(',');
  Serial.print(-42);
  Serial.print(',');
  Serial.println(1.5, 2);

  /* Rounds at the digit asked for rather than truncating. */
  Serial.print("round=");
  Serial.println(3.14159, 4);

  /* The example Print.cpp's own comment gives: 1.999 at two digits carries
   * into the integer part. */
  Serial.print("carry=");
  Serial.println(1.999, 2);

  /* Zero digits: rounded, and no trailing point. */
  Serial.print("digits=");
  Serial.println(2.5, 0);

  tc_done();
}

void setup()
{
  tc_begin("print_format");
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
