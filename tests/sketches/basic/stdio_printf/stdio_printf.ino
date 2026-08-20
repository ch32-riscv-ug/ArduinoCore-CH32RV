// stdio must reach the serial monitor.
//
// Two separate bugs made this a silent no-op on every board, and both are the
// kind that a compile test cannot see:
//
//  1. libgloss ships *semihosting* syscall stubs - its _write issues `ecall`,
//     which on a CH32 with no environment-call handler traps to the reset
//     vector. libgloss is scanned after core.a, so plain left-to-right symbol
//     resolution picked it over ours. platform.txt now links core.a inside
//     --start-group/--end-group.
//  2. HardwareSerial.cpp includes its own header before Arduino.h, and the
//     header only learned SERIAL_PORT_MONITOR from the variant via Arduino.h.
//     For exactly that one translation unit the macro was undefined, so the
//     printf() bridge compiled to `return 0`. HardwareSerial.h now includes
//     pins_arduino.h itself.
//
// %f is deliberately not exercised here: ADR-0004 makes newlib-nano the default
// runtime, which drops float conversion unless the printf menu is set to
// "float". A sketch that assumed %f worked would fail on the default FQBN.

#include <stdio.h>
#include <unistd.h>

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("stdio test start");

  // The lowest level first: if this is libgloss's stub the board resets here
  // and nothing below is ever printed.
  ssize_t n = write(STDOUT_FILENO, "write=direct\r\n", 14);
  Serial.print("write returned ");
  Serial.println((int)n);

  int p = printf("printf=%d %s %c\r\n", 42, "str", 'x');
  Serial.print("printf returned ");
  Serial.println(p);

  int u = puts("puts=line");
  Serial.print("puts returned ");
  Serial.println(u >= 0 ? "ok" : "BAD");

  // A conversion wide enough to need the buffer newlib mallocs for stdout.
  printf("wide=%08lx\r\n", 0xDEADBEEFUL);
  fflush(stdout);

  Serial.println("stdio test done");
}

void loop()
{
}
