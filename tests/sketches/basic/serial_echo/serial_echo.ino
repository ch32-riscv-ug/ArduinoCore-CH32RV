// Serial receive, which serial_println cannot cover: it only proves TX.
// The host sends lines, the target answers, so a pass means both directions of
// the UART and the RX interrupt path work.
//
// Needs the probe's UART TX wired to the board's Serial RX pin as well as the
// usual TX. The uart_scan manual test only finds TX, so RX is what this adds.

void setup() {
  Serial.begin(115200);
  Serial.println("echo ready");
}

static String line;

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "ping") {
          Serial.println("pong");
        } else if (line == "len") {
          Serial.print("len=");
          Serial.println(line.length());
        } else {
          Serial.print("echo:");
          Serial.println(line);
        }
        line = "";
      }
    } else if (line.length() < 64) {
      line += c;
    }
  }
}
