/* UsartSPI (tests/instrumented/uart/test_uart_sync.py): Serial's USART in its
 * synchronous mode as the controller of the probe's spi-target. Console on
 * SerialDMSeq; every command answers one line:
 *
 *   ready <nonce>             READY <nonce> <build id> <requests>
 *   usync <cs> | usync off    UsartSPI on Serial's route, CS (a GPIO) high: USYNC <ck> <rx> <tx>
 *                             (USYNC NONE: no CK on the route, or no synchronous mode); off: the
 *                             bus ended and Serial at 115200 8N1 again: USYNC OFF
 *   uxfer <mode> <lsb> <hz> <word> <bytes>
 *                             spi.ino's xfer on the UsartSPI: UXFER <bytes read, hex> <word read,
 *                             hex> <the clock>
 *
 * Its own case (uart-sync) so that uart.ino keeps CH32V003's 16 KB. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <UsartSPI.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

uint32_t requests;
char line[80];
uint8_t used;

UsartSPI usartBus(Serial);
int syncCs = -1;

void reply(const char *command) {
  ++requests;
  if (strncmp(command, "ready ", 6) == 0) {
    SerialDMSeq.print("READY ");
    SerialDMSeq.print(command + 6);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(BENCH_BUILD_ID);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(requests);
  } else if (strncmp(command, "usync ", 6) == 0) {
    // usync <cs> | usync off: UsartSPI on Serial's route, CS (a GPIO) high - USYNC <ck> <rx> <tx> (USYNC NONE: no CK
    // on the route, or no synchronous mode); off: the bus ended and Serial at 115200 8N1 again - USYNC OFF.
    if (strcmp(command + 6, "off") == 0) {
      usartBus.end();
      Serial.begin(115200);
      SerialDMSeq.println("USYNC OFF");
    } else {
      syncCs = (int)strtol(command + 6, nullptr, 10);
      pinMode(syncCs, OUTPUT);
      digitalWrite(syncCs, HIGH);
      if (usartBus.begin(-1, -1, -1, -1)) {
        SerialDMSeq.print("USYNC ");
        SerialDMSeq.print(Serial.synchronousClockPin());
        SerialDMSeq.print(' ');
        SerialDMSeq.print(PIN_SERIAL_RX);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(PIN_SERIAL_TX);
      } else {
        SerialDMSeq.println("USYNC NONE");
      }
    }
  } else if (strncmp(command, "uxfer ", 6) == 0) {
    // uxfer <mode> <lsb> <hz> <word> <bytes>: spi.ino's xfer on the UsartSPI - SPISettings(hz, order, mode), CS low,
    // transfer() each byte (hex), transfer16(word, hex), CS high: UXFER <bytes read, hex> <word read, hex> <clock>.
    char *p = (char *)command + 6;
    const unsigned long mode = strtoul(p, &p, 10);
    const unsigned long lsb = strtoul(p, &p, 10);
    const unsigned long hz = strtoul(p, &p, 10);
    const uint16_t word = (uint16_t)strtoul(p, &p, 16);
    while (*p == ' ') ++p;
    uint8_t out[16];
    size_t n = 0;
    for (; n < sizeof out && p[0] && p[1]; p += 2) {
      char two[3] = {p[0], p[1], 0};
      out[n++] = (uint8_t)strtoul(two, nullptr, 16);
    }
    usartBus.beginTransaction(SPISettings(hz, lsb ? LSBFIRST : MSBFIRST, (uint8_t)mode));
    digitalWrite(syncCs, LOW);
    for (size_t i = 0; i < n; ++i) out[i] = usartBus.transfer(out[i]);
    const uint16_t in = usartBus.transfer16(word);
    digitalWrite(syncCs, HIGH);
    usartBus.endTransaction();
    SerialDMSeq.print("UXFER ");
    for (size_t i = 0; i < n; ++i) {
      if (out[i] < 16) SerialDMSeq.print('0');
      SerialDMSeq.print(out[i], HEX);
    }
    SerialDMSeq.print(' ');
    for (int shift = 12; shift >= 0; shift -= 4) SerialDMSeq.print((in >> shift) & 0xF, HEX);
    SerialDMSeq.print(' ');
    SerialDMSeq.println(usartBus.clockHz());
  } else {
    SerialDMSeq.println("ERROR COMMAND");
  }
}

void setup() {
  Serial.begin(115200);
  SerialDMSeq.begin(115200);
  SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
  SerialDMSeq.println(CH32RV.resetReasonName());
}

void loop() {
  while (SerialDMSeq.available()) {
    int c = SerialDMSeq.read();
    if (c == '\n') {
      line[used] = 0;
      if (used) reply(line);
      used = 0;
    } else if (c != '\r' && used < sizeof(line) - 1) {
      line[used++] = c;
    }
  }
}
