/* Wire contracts (tests/instrumented/wire): Wire as the controller of an
 * OEP probe's i2c-target, at 100 kHz on the default pins. The probe also
 * captures SDA and SCL, so the test sees which conditions went on the wire.
 * Console on SerialDMSeq:
 *
 *   ready <nonce>         READY <nonce> <build id> <sda pin> <scl pin> (Wire's)
 *   soft <sda> <scl>      run the sequences on a SoftWire on these pins instead
 *                         of Wire: SOFT <1 opened / 0 refused>
 *   seq <addr> <n> <stop> write 0x10 with endTransmission(false), then
 *                         requestFrom(addr, n, stop), then write 0x20 with
 *                         endTransmission(true), back to back:
 *                         SEQ <first result> <got> <hex bytes or -> <last result>
 *   nack <addr>           with no target: write 0x10 to addr, endTransmission(true):
 *                         NACK <result> (2: the address was not acknowledged) */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <Wire.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

/* The bus the sequences run on: Wire (hardware, or a SoftWire when the board
 * menu sets its I2C to Software), or the SoftWire `soft` names. */
static TwoWire *bus = &Wire;
static SoftWire soft(CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN);

static void sequence(uint8_t address, size_t n, bool stop)
{
    bus->beginTransmission(address);
    bus->write(0x10);
    const uint8_t first = bus->endTransmission(false);
    const size_t got = bus->requestFrom(address, n, stop);
    uint8_t data[8];
    size_t kept = 0;
    while (bus->available() && kept < sizeof data) {
        data[kept++] = (uint8_t)bus->read();
    }
    bus->beginTransmission(address);
    bus->write(0x20);
    const uint8_t last = bus->endTransmission(true);

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
        } else if (strncmp(line, "soft ", 5) == 0) {
            char *p = line + 5;
            const long sda = strtol(p, &p, 10);
            const long scl = strtol(p, &p, 10);
            const bool ok = soft.begin((int)sda, (int)scl, 100000);
            if (ok) {
                bus = &soft;
            }
            SerialDMSeq.print("SOFT ");
            SerialDMSeq.println(ok ? 1 : 0);
        } else if (strncmp(line, "nack ", 5) == 0) {
            const unsigned long address = strtoul(line + 5, nullptr, 0);
            bus->beginTransmission((uint8_t)address);
            bus->write(0x10);
            SerialDMSeq.print("NACK ");
            SerialDMSeq.println(bus->endTransmission(true));
        } else if (strncmp(line, "seq ", 4) == 0) {
            char *p = line + 4;
            const unsigned long address = strtoul(p, &p, 0);
            const unsigned long n = strtoul(p, &p, 10);
            const unsigned long stop = strtoul(p, &p, 10);
            sequence((uint8_t)address, n, stop != 0);
        }
    }
}
