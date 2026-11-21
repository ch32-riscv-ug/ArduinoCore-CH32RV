/* Wire contracts (tests/instrumented/wire): Wire as the controller of an
 * OEP probe's i2c-target, at 100 kHz on the default pins. The probe also
 * captures SDA and SCL, so the test sees which conditions went on the wire.
 * Console on CONSOLE (bench_console.h):
 *
 *   ready <nonce>         READY <nonce> <build id> <sda pin> <scl pin> (Wire's)
 *   end                   end() the bus that is the target (its lines let go): END
 *   controller            Wire anew as a controller only (a test before may have
 *                         left it a target): CONTROLLER
 *   soft <sda> <scl>      run the sequences on a SoftWire on these pins instead
 *                         of Wire: SOFT <1 opened / 0 refused>
 *   seq <addr> <n> <stop> write 0x10 with endTransmission(false), then
 *                         requestFrom(addr, n, stop), then write 0x20 with
 *                         endTransmission(true), back to back:
 *                         SEQ <first result> <got> <hex bytes or -> <last result>
 *   nack <addr>           with no target: write 0x10 to addr, endTransmission(true):
 *                         NACK <result> (2: the address was not acknowledged)
 *   clock <hz>            the bus's setClock(hz) (100 kHz from start-up): CLOCK <hz>
 *
 * The bus as a target of the probe's i2c-controller (begin(address)): Wire, or
 * the SoftWire `soft` named (on its pins):
 *
 *   slave <addr>          end(), begin(addr) with onReceive / onRequest:
 *                         SLAVE 1 (SLAVE 0 where the bus refused: a SoftWire whose
 *                         SDA and SCL share an EXTI line)
 *   sreply <hex or ->     what onRequest() write()s from now on (- : nothing)
 *   sgot                  SGOT <onReceive calls> <onRequest calls> <the order, R / Q per
 *                         call, last 8> <the last onReceive's count> <its bytes, hex or ->
 *
 * Single transfers and the bus's settings, for the faults and the extensions
 * (an address is 7-bit, or 10-bit with ten 1: WIRE_10BIT):
 *
 *   w <addr> <ten> <hex or -> <stop>
 *                         beginTransmission, write the bytes, endTransmission(stop):
 *                         W <result> <the timeout flag, cleared after>
 *   r <addr> <ten> <n> <stop>
 *                         requestFrom(addr, n, stop): R <got> <hex bytes or ->
 *   timeout <us>          setWireTimeout(us): TIMEOUT <us>
 *   pec <0/1>             setPEC(): PEC <0/1>
 *   slave <addr> [ten]    as above, a 10-bit address with ten 1
 *   gc <0/1>              the target's setGeneralCall(): GC <0/1>
 *   strand <addr> <bits>  close the bus and, on its pins, bit-bang START, a read of
 *                         addr, its ACK and <bits> clocks of the byte the target
 *                         sends, leaving SCL high in the last bit - a target cut off
 *                         mid-byte, holding SDA low if that bit is 0 - then reopen the
 *                         bus (no transfer): STRAND <ack 1/0> <SDA level 1/0>
 *   contend <ms> <addr>   for <ms>, back-to-back 1-byte writes to addr (the bus a
 *                         target as well, answering the probe in between):
 *                         CONTEND <results 0> <2> <3> <4> <5> <other> */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
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
/* The pins of the bus the sequences run on (strand). */
static int busSda = PIN_WIRE_SDA, busScl = PIN_WIRE_SCL;
/* The target the callbacks read from and write to. */
static TwoWire *target = &Wire;

/* The target side: what onReceive() got, what onRequest() sends, and in which order they ran. */
static volatile uint32_t receives, requests;
static volatile int lastCount;
static uint8_t lastBytes[40];
static uint8_t replyBytes[16];
static volatile uint8_t replyLength;
static char order[9];
static volatile uint8_t orderUsed;

static void noteOrder(char c)
{
    if (orderUsed == sizeof order - 1) {
        memmove(order, order + 1, sizeof order - 2);
        orderUsed--;
    }
    order[orderUsed++] = c;
    order[orderUsed] = 0;
}

static void onReceived(int count)
{
    receives = receives + 1;
    lastCount = count;
    int i = 0;
    while (target->available()) {
        const int b = target->read();
        if (i < (int)sizeof lastBytes) {
            lastBytes[i++] = (uint8_t)b;
        }
    }
    noteOrder('R');
}

static void onRequested()
{
    requests = requests + 1;
    if (replyLength) {
        target->write(replyBytes, replyLength);
    }
    noteOrder('Q');
}

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

    CONSOLE.print("SEQ ");
    CONSOLE.print(first);
    CONSOLE.print(' ');
    CONSOLE.print((unsigned)got);
    CONSOLE.print(' ');
    if (!kept) {
        CONSOLE.print('-');
    }
    for (size_t i = 0; i < kept; i++) {
        if (data[i] < 0x10) {
            CONSOLE.print('0');
        }
        CONSOLE.print(data[i], HEX);
    }
    CONSOLE.print(' ');
    CONSOLE.println(last);
}

static uint16_t addressOf(unsigned long address, unsigned long ten)
{
    return ten ? (uint16_t)(address | WIRE_10BIT) : (uint16_t)address;
}

static void printHex(const uint8_t *data, size_t n)
{
    if (!n) {
        CONSOLE.print('-');
    }
    for (size_t i = 0; i < n; i++) {
        CONSOLE.print((char)"0123456789abcdef"[data[i] >> 4]);
        CONSOLE.print((char)"0123456789abcdef"[data[i] & 15]);
    }
}

static size_t parseHex(const char *p, uint8_t *out, size_t room)
{
    size_t n = 0;
    while (p[0] && p[1] && p[0] != '-' && p[0] != ' ' && n < room) {
        const char pair[3] = {p[0], p[1], 0};
        out[n++] = (uint8_t)strtoul(pair, nullptr, 16);
        p += 2;
    }
    return n;
}

/* A target cut off in the middle of the byte it sends: START, the address
 * with R, its ACK, `bits` clocks of the data, SCL left high. At about 10 kHz,
 * the pins open-drain outputs (the bus closed meanwhile). */
static void strand(uint8_t address, uint8_t bits)
{
    bus->end();
    const int sda = busSda, scl = busScl;
    pinMode(sda, OUTPUT_OPENDRAIN);
    pinMode(scl, OUTPUT_OPENDRAIN);
    digitalWrite(sda, HIGH);
    digitalWrite(scl, HIGH);
    delayMicroseconds(100);
    digitalWrite(sda, LOW);                       /* START */
    delayMicroseconds(50);
    digitalWrite(scl, LOW);
    const uint8_t byte = (uint8_t)((address << 1) | 1u);
    for (int8_t b = 7; b >= 0; b--) {
        digitalWrite(sda, (byte >> b) & 1u ? HIGH : LOW);
        delayMicroseconds(50);
        digitalWrite(scl, HIGH);
        delayMicroseconds(50);
        digitalWrite(scl, LOW);
    }
    digitalWrite(sda, HIGH);                      /* the target's ACK */
    delayMicroseconds(50);
    digitalWrite(scl, HIGH);
    delayMicroseconds(25);
    const bool ack = digitalRead(sda) == LOW;
    delayMicroseconds(25);
    digitalWrite(scl, LOW);
    for (uint8_t k = 0; k < bits; k++) {
        delayMicroseconds(50);
        digitalWrite(scl, HIGH);
        delayMicroseconds(50);
        digitalWrite(scl, LOW);
    }
    delayMicroseconds(50);
    digitalWrite(scl, HIGH);                      /* the next bit's rise: left there */
    delayMicroseconds(50);
    const bool level = digitalRead(sda) == HIGH;
    if (bus == &soft) {
        soft.begin(busSda, busScl, 0);
    } else {
        Wire.begin();
    }
    CONSOLE.print("STRAND ");
    CONSOLE.print(ack ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println(level ? 1 : 0);
}

static char line[96];
static uint8_t used;

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
    Wire.begin();
    Wire.setClock(100000);
}

void loop()
{
    while (CONSOLE.available()) {
        const int ch = CONSOLE.read();
        if (ch != '\n') {
            if (ch != '\r' && used < sizeof line - 1) {
                line[used++] = (char)ch;
            }
            continue;
        }
        line[used] = 0;
        used = 0;
        if (strncmp(line, "ready ", 6) == 0) {
            CONSOLE.print("READY ");
            CONSOLE.print(line + 6);
            CONSOLE.print(' ');
            CONSOLE.print(BENCH_BUILD_ID);
            CONSOLE.print(' ');
            CONSOLE.print(PIN_WIRE_SDA);
            CONSOLE.print(' ');
            CONSOLE.println(PIN_WIRE_SCL);
        } else if (strcmp(line, "end") == 0) {
            target->end();
            CONSOLE.println("END");
        } else if (strcmp(line, "controller") == 0) {
            Wire.end();
            Wire.begin();
            CONSOLE.println("CONTROLLER");
        } else if (strncmp(line, "soft ", 5) == 0) {
            char *p = line + 5;
            const long sda = strtol(p, &p, 10);
            const long scl = strtol(p, &p, 10);
            const bool ok = soft.begin((int)sda, (int)scl, 100000);
            if (ok) {
                bus = &soft;
                busSda = (int)sda;
                busScl = (int)scl;
            }
            CONSOLE.print("SOFT ");
            CONSOLE.println(ok ? 1 : 0);
        } else if (strncmp(line, "nack ", 5) == 0) {
            const unsigned long address = strtoul(line + 5, nullptr, 0);
            bus->beginTransmission((uint8_t)address);
            bus->write(0x10);
            CONSOLE.print("NACK ");
            CONSOLE.println(bus->endTransmission(true));
        } else if (strncmp(line, "clock ", 6) == 0) {
            const unsigned long hz = strtoul(line + 6, nullptr, 0);
            bus->setClock((uint32_t)hz);
            CONSOLE.print("CLOCK ");
            CONSOLE.println(hz);
        } else if (strncmp(line, "slave ", 6) == 0) {
            char *p = line + 6;
            const unsigned long address7 = strtoul(p, &p, 0);
            const uint16_t address = addressOf(address7, strtoul(p, &p, 10));
            receives = requests = 0;
            lastCount = 0;
            orderUsed = 0;
            order[0] = 0;
            target = bus;
            target->end();
            target->onReceive(onReceived);
            target->onRequest(onRequested);
            bool ok = true;
            if (bus == &soft) {
                ok = soft.begin(address, -1, -1, 0);   /* on the pins `soft` named */
            } else {
                Wire.begin(address);
            }
            CONSOLE.print("SLAVE ");
            CONSOLE.println(ok ? 1 : 0);
        } else if (strncmp(line, "sreply ", 7) == 0) {
            const char *p = line + 7;
            uint8_t n = 0;
            while (p[0] && p[1] && p[0] != '-' && n < sizeof replyBytes) {
                const char pair[3] = {p[0], p[1], 0};
                replyBytes[n++] = (uint8_t)strtoul(pair, nullptr, 16);
                p += 2;
            }
            replyLength = n;
            CONSOLE.print("SREPLY ");
            CONSOLE.println(n);
        } else if (strcmp(line, "sgot") == 0) {
            CONSOLE.print("SGOT ");
            CONSOLE.print((unsigned long)receives);
            CONSOLE.print(' ');
            CONSOLE.print((unsigned long)requests);
            CONSOLE.print(' ');
            CONSOLE.print(orderUsed ? order : "-");
            CONSOLE.print(' ');
            CONSOLE.print(lastCount);
            CONSOLE.print(' ');
            const int shown = lastCount < (int)sizeof lastBytes ? lastCount : (int)sizeof lastBytes;
            for (int i = 0; i < shown; i++) {
                CONSOLE.print((char)"0123456789abcdef"[lastBytes[i] >> 4]);
                CONSOLE.print((char)"0123456789abcdef"[lastBytes[i] & 15]);
            }
            CONSOLE.println(shown ? "" : "-");
        } else if (strncmp(line, "w ", 2) == 0) {
            char *p = line + 2;
            const unsigned long address = strtoul(p, &p, 0);
            const uint16_t a = addressOf(address, strtoul(p, &p, 10));
            while (*p == ' ') {
                p++;
            }
            uint8_t data[40];
            const size_t n = parseHex(p, data, sizeof data);
            while (*p && *p != ' ') {
                p++;
            }
            const unsigned long stop = strtoul(p, &p, 10);
            bus->beginTransmission(a);
            bus->write(data, n);
            const uint8_t rc = bus->endTransmission(stop != 0);
            CONSOLE.print("W ");
            CONSOLE.print(rc);
            CONSOLE.print(' ');
            CONSOLE.println(bus->getWireTimeoutFlag() ? 1 : 0);
            bus->clearWireTimeoutFlag();
        } else if (strncmp(line, "r ", 2) == 0) {
            char *p = line + 2;
            const unsigned long address = strtoul(p, &p, 0);
            const uint16_t a = addressOf(address, strtoul(p, &p, 10));
            const unsigned long n = strtoul(p, &p, 10);
            const unsigned long stop = strtoul(p, &p, 10);
            const size_t got = bus->requestFrom(a, (size_t)n, stop != 0);
            uint8_t data[CH32RV_WIRE_BUFFER_SIZE];
            size_t kept = 0;
            while (bus->available() && kept < sizeof data) {
                data[kept++] = (uint8_t)bus->read();
            }
            CONSOLE.print("R ");
            CONSOLE.print((unsigned)got);
            CONSOLE.print(' ');
            printHex(data, kept);
            CONSOLE.println();
        } else if (strncmp(line, "timeout ", 8) == 0) {
            const unsigned long us = strtoul(line + 8, nullptr, 10);
            bus->setWireTimeout(us);
            CONSOLE.print("TIMEOUT ");
            CONSOLE.println(us);
        } else if (strncmp(line, "pec ", 4) == 0) {
            const bool on = strtoul(line + 4, nullptr, 10) != 0;
            bus->setPEC(on);
            CONSOLE.print("PEC ");
            CONSOLE.println(bus->getPEC() ? 1 : 0);
        } else if (strncmp(line, "gc ", 3) == 0) {
            const bool on = strtoul(line + 3, nullptr, 10) != 0;
            if (bus == &soft) {
                soft.setGeneralCall(on);
            } else {
                Wire.setGeneralCall(on);
            }
            CONSOLE.print("GC ");
            CONSOLE.println(on ? 1 : 0);
        } else if (strncmp(line, "strand ", 7) == 0) {
            char *p = line + 7;
            const unsigned long address = strtoul(p, &p, 0);
            const unsigned long bits = strtoul(p, &p, 10);
            strand((uint8_t)address, (uint8_t)bits);
        } else if (strncmp(line, "contend ", 8) == 0) {
            char *p = line + 8;
            const unsigned long ms = strtoul(p, &p, 10);
            const uint16_t a = (uint16_t)strtoul(p, &p, 0);
            uint32_t counts[6] = {0, 0, 0, 0, 0, 0};   /* 0, 2, 3, 4, 5, other */
            const uint32_t t0 = millis();
            uint8_t i = 0;
            while (millis() - t0 < ms) {
                bus->beginTransmission(a);
                bus->write(i++);
                const uint8_t rc = bus->endTransmission(true);
                counts[rc == 0 ? 0 : rc >= 2 && rc <= 5 ? rc - 1 : 5]++;
            }
            CONSOLE.print("CONTEND");
            for (uint8_t k = 0; k < 6; k++) {
                CONSOLE.print(' ');
                CONSOLE.print((unsigned long)counts[k]);
            }
            CONSOLE.println();
        } else if (strncmp(line, "seq ", 4) == 0) {
            char *p = line + 4;
            const unsigned long address = strtoul(p, &p, 0);
            const unsigned long n = strtoul(p, &p, 10);
            const unsigned long stop = strtoul(p, &p, 10);
            sequence((uint8_t)address, n, stop != 0);
        }
    }
}
