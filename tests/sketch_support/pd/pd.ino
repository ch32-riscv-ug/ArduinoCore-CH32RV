/* USB PD sink contracts (tests/instrumented/pd): CH32RVUsbPd against the
 * charger on the DUT's USB-C. Console on SerialDMSeq; loop() calls
 * USBPD.maintain() all the time, so a PPS contract stays up between commands.
 *
 *   ready <nonce>             READY <nonce> <build id> <begin 0/1> <CC1 pin> <CC2 pin>
 *   wait <ms>                 STATE ... once ready() or after <ms>
 *   state                     STATE <connected 0/1> <ready 0/1> <mV> <mA> <profile> <pps 0/1>
 *   caps                      CAPS <count> then one "PDO <i> <kind> <min mV> <max mV> <max mA>" per profile
 *   req <mV> [mA]             REQ <0/1> <mV> <mA> <profile>
 *   reqp <i> <mV> [mA]        REQP <0/1> <mV> <mA> <profile>  (requestProfile)
 *   vbus <pin>                VBUS <mV at the pin> (analogRead, scaled by the measured VDD)
 *   restart                   RESTART <begin 0/1>: end() and begin() under a live contract */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbPd.h>
#include <stdlib.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static bool begun;
static char line[64];
static uint8_t used;

static void say(const char *head, const long *v, uint8_t n)
{
    SerialDMSeq.print(head);
    for (uint8_t i = 0; i < n; i++) {
        SerialDMSeq.print(' ');
        SerialDMSeq.print(v[i]);
    }
    SerialDMSeq.println();
}

static void state()
{
    const long v[6] = {USBPD.connected(), USBPD.ready(), USBPD.voltage(), USBPD.current(), USBPD.contractProfile(),
                       USBPD.pps()};
    say("STATE", v, 6);
}

static void contract(const char *head, bool ok)
{
    const long v[4] = {ok, USBPD.voltage(), USBPD.current(), USBPD.contractProfile()};
    say(head, v, 4);
}

static void reply(char *command)
{
    char *end;
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
#ifdef CH32RV_USBPD_BASE
        const long v[3] = {begun, (long)CH32RV_USBPD_CC1_PIN, (long)CH32RV_USBPD_CC2_PIN};
#else
        const long v[3] = {begun, -1, -1};
#endif
        say("", v, 3);
    } else if (strncmp(command, "wait ", 5) == 0) {
        const uint32_t ms = strtoul(command + 5, nullptr, 10), t0 = millis();
        while (!USBPD.ready() && millis() - t0 < ms) {
        }
        state();
    } else if (strcmp(command, "state") == 0) {
        state();
    } else if (strcmp(command, "caps") == 0) {
        const uint8_t n = USBPD.profileCount();
        const long c = n;
        say("CAPS", &c, 1);
        for (uint8_t i = 0; i < n; i++) {
            const PDProfile p = USBPD.profile(i);
            const long v[5] = {i, p.kind, p.min_mv, p.max_mv, p.max_ma};
            say("PDO", v, 5);
        }
    } else if (strncmp(command, "req ", 4) == 0) {
        const uint16_t mv = (uint16_t)strtoul(command + 4, &end, 10), ma = (uint16_t)strtoul(end, nullptr, 10);
        contract("REQ", USBPD.request(mv, ma));
    } else if (strncmp(command, "reqp ", 5) == 0) {
        const uint8_t i = (uint8_t)strtoul(command + 5, &end, 10);
        const uint16_t mv = (uint16_t)strtoul(end, &end, 10), ma = (uint16_t)strtoul(end, nullptr, 10);
        contract("REQP", USBPD.requestProfile(i, mv, ma));
    } else if (strncmp(command, "vbus ", 5) == 0) {
        const pin_size_t pin = (pin_size_t)strtoul(command + 5, nullptr, 10);
        analogReadResolution(12);
        const uint32_t vdd = ch32rv_adc_vdd_millivolts();
        uint32_t sum = 0;
        for (uint8_t i = 0; i < 16; i++) {
            sum += (uint32_t)analogRead(pin);
        }
        const long v = (long)((uint64_t)sum * vdd / 16u / 4095u);
        say("VBUS", &v, 1);
    } else if (strcmp(command, "restart") == 0) {
        USBPD.end();
        begun = USBPD.begin();
        const long v = begun;
        say("RESTART", &v, 1);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    begun = USBPD.begin();
}

void loop()
{
    USBPD.maintain();
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
