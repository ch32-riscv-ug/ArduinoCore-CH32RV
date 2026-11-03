/* Wire contracts (tests/instrumented/wire): Wire as the controller of an
 * OEP probe's i2c-target, at 100 kHz on the default pins. The probe also
 * captures SDA and SCL, so the test sees which conditions went on the wire.
 * Console on SerialDMSeq:
 *
 *   ready <nonce>         READY <nonce> <build id> <sda pin> <scl pin>
 *   seq <addr> <n> <stop> write 0x10 with endTransmission(false), then
 *                         requestFrom(addr, n, stop), then write 0x20 with
 *                         endTransmission(true), back to back:
 *                         SEQ <first result> <got> <hex bytes or -> <last result> */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <Wire.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static void sequence(uint8_t address, size_t n, bool stop)
{
    Wire.beginTransmission(address);
    Wire.write(0x10);
    const uint8_t first = Wire.endTransmission(false);
    const size_t got = Wire.requestFrom(address, n, stop);
    uint8_t data[8];
    size_t kept = 0;
    while (Wire.available() && kept < sizeof data) {
        data[kept++] = (uint8_t)Wire.read();
    }
    Wire.beginTransmission(address);
    Wire.write(0x20);
    const uint8_t last = Wire.endTransmission(true);

    SerialDMSeq.print("SEQ ");
    SerialDMSeq.print(first);
    SerialDMSeq.print(' ');
    SerialDMSeq.print((unsigned)got);
    SerialDMSeq.print(' ');
    if (!kept) {
        SerialDMSeq.print('-');
    }
    for (size_t i = 0; i < kept; i++) {
        if (data[i] < 0x10) {
            SerialDMSeq.print('0');
        }
        SerialDMSeq.print(data[i], HEX);
    }
    SerialDMSeq.print(' ');
    SerialDMSeq.println(last);
}

static char line[48];
static uint8_t used;

void setup()
{
    SerialDMSeq.begin(115200);
    Wire.begin();
    Wire.setClock(100000);
}

void loop()
{
    while (SerialDMSeq.available()) {
        const int ch = SerialDMSeq.read();
        if (ch != '\n') {
            if (ch != '\r' && used < sizeof line - 1) {
                line[used++] = (char)ch;
            }
            continue;
        }
        line[used] = 0;
        used = 0;
        if (strncmp(line, "ready ", 6) == 0) {
            SerialDMSeq.print("READY ");
            SerialDMSeq.print(line + 6);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(BENCH_BUILD_ID);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(PIN_WIRE_SDA);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(PIN_WIRE_SCL);
        } else if (strncmp(line, "seq ", 4) == 0) {
            char *p = line + 4;
            const unsigned long address = strtoul(p, &p, 0);
            const unsigned long n = strtoul(p, &p, 10);
            const unsigned long stop = strtoul(p, &p, 10);
            sequence((uint8_t)address, n, stop != 0);
        }
    }
}
