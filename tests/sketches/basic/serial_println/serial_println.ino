// The UART transmit path, and the gate for "a UART works" on every board.
//
// The host names the port and route over Console - "UART <n> <route> <baud>",
// picked from the pins it can actually listen on - and the lines below go out
// on that UART, where the host reads them off the wire. The verdict comes back
// on Console. Nothing here chooses pins: that is the host's knowledge, not the
// board's (see testcmd.h).
//
// RUN is what makes it a gate rather than a coin toss: the lines are printed
// after the host has proved the link with PING/PONG, so they cannot be confused
// with the last sketch's output.
#include "testcmd.h"

static void run_checks()
{
  arduino::CH32HardwareSerial *uart = tc_uart();
  if (!uart) {
    tc_skip("uart_tx", "no UART named");
    tc_done();
    return;
  }
  uart->println("hello from ch32");
  uart->print("int=");
  uart->println(42);
  uart->print("hex=");
  uart->println(0xBEEF, HEX);

  /* Room in the transmit ring. Print's default returns 0, which would make a
   * sketch believe the port is permanently full.
   *
   * flush() first: at 115200 the ring really is full after the lines above.
   * Measuring without draining would be testing how fast the UART is, not
   * whether the count is reported. */
  uart->flush();
  const int room = uart->availableForWrite();
  tc_checkv("availableForWrite", room > 0, room);
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
  if (tc_uart_command(cmd)) {
    return;
  }
  if (!strcmp(cmd, "RUN")) {
    run_checks();
  } else {
    tc_unknown(cmd);
  }
}
