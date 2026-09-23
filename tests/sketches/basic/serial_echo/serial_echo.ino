// The UART receive path, which serial_println cannot cover: it only proves TX.
// The host sends lines on the UART it named, and the board answers on the same
// UART, so a pass means both directions and the RX interrupt path work. The
// harness - the banner, and naming the UART - stays on Console.
//
//   ECHO <text>   ->  echo:<text>
//   LEN <text>    ->  len=<count>
//   anything else ->  unknown:<line>
#include "testcmd.h"

/* Argument of "<verb> <text>", or NULL when the verb does not match.
 * Pointer arithmetic into the line buffer: no copy, no allocation. */
static const char *argument(const char *line, const char *verb)
{
  const size_t n = strlen(verb);
  if (strncmp(line, verb, n) != 0 || line[n] != ' ') {
    return NULL;
  }
  return line + n + 1;
}

static void answer(Print &out, const char *line)
{
  const char *text = argument(line, "ECHO");
  if (text) {
    out.print("echo:");
    out.println(text);
    return;
  }
  text = argument(line, "LEN");
  if (text) {
    out.print("len=");
    out.println((unsigned)strlen(text));
    return;
  }
  out.print("unknown:");
  out.println(line);
}

/* One line at a time off the UART under test. A line longer than the buffer
 * keeps its head, and comes back as unknown rather than being lost. */
static void serve(arduino::CH32HardwareSerial &uart)
{
  static char buf[TC_CMD_MAX];
  static uint8_t len;
  while (uart.available()) {
    const char c = (char)uart.read();
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (len < sizeof buf - 1) {
        buf[len++] = c;
      }
      continue;
    }
    buf[len] = '\0';
    len = 0;
    if (buf[0] != '\0') {
      answer(uart, buf);
    }
  }
}

void setup()
{
  tc_begin("serial_echo");
}

void loop()
{
  const char *cmd = tc_ready();
  if (cmd && !tc_uart_command(cmd)) {
    tc_unknown(cmd);
  }
  if (tc_uart()) {
    serve(*tc_uart());
  }
}
