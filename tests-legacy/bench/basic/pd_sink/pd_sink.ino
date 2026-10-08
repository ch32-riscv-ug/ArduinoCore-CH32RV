/* pd_sink - the USB PD sink against a real charger, one command at a time.
 *
 * The host drives it (tests-legacy/bench/basic/pd_sink/test_pd_sink.py) and judges
 * every answer; the sketch only does what it is told and says what happened.
 * Needs a PD charger on the board's USB-C (the bench file says so with
 * facts.pd_source); on a part without the USBPD block BEGIN answers ok=0.
 *
 *   PD BEGIN            begin(), then wait on ready() alone (up to 3 s)
 *   PD LIST             the charger's profiles, one line each
 *   PD REQ <mv> <ma>    request(mv, ma)
 *   PD PROF <i> <mv> <ma>  requestProfile(i, mv, ma)
 *   PD HOLD <ms>        call maintain() for ms (a PPS contract must survive)
 *   PD END              end()
 */
#include <USBPD.h>

#include "testcmd.h"

static void state(const char *what, long ok, uint32_t ms)
{
    Console.print(what);
    Console.print(" ok=");
    Console.print(ok);
    Console.print(" ready=");
    Console.print(USBPD.ready() ? 1 : 0);
    Console.print(" connected=");
    Console.print(USBPD.connected() ? 1 : 0);
    Console.print(" v=");
    Console.print(USBPD.voltage());
    Console.print(" a=");
    Console.print(USBPD.current());
    Console.print(" idx=");
    Console.print(USBPD.contractProfile());
    Console.print(" pps=");
    Console.print(USBPD.pps() ? 1 : 0);
    Console.print(" n=");
    Console.print(USBPD.profileCount());
    Console.print(" ms=");
    Console.println(ms);
}

static void list()
{
    for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
        PDProfile p = USBPD.profile(i);
        Console.print("PD PDO ");
        Console.print(i);
        Console.print(" kind=");
        Console.print(p.kind);
        Console.print(" min=");
        Console.print(p.min_mv);
        Console.print(" max=");
        Console.print(p.max_mv);
        Console.print(" ma=");
        Console.print(p.max_ma);
        Console.print(" raw=");
        Console.println(p.raw, HEX);
    }
    Console.print("PD LIST end n=");
    Console.println(USBPD.profileCount());
}

void setup()
{
    tc_begin("pd_sink");
}

void loop()
{
    USBPD.maintain();
    const char *cmd = tc_ready();
    if (!cmd) {
        return;
    }
    long a = 0, b = 0, c = 0;
    const uint32_t t0 = millis();
    if (!strcmp(cmd, "PD BEGIN")) {
        const bool ok = USBPD.begin();
        while (ok && !USBPD.ready() && millis() - t0 < 3000) {
        }
        state("PD BEGIN", ok, millis() - t0);
    } else if (!strcmp(cmd, "PD LIST")) {
        list();
    } else if (sscanf(cmd, "PD REQ %ld %ld", &a, &b) == 2) {
        const bool ok = USBPD.request((uint16_t)a, (uint16_t)b);
        state("PD REQ", ok, millis() - t0);
    } else if (sscanf(cmd, "PD PROF %ld %ld %ld", &a, &b, &c) == 3) {
        const bool ok = USBPD.requestProfile((uint8_t)a, (uint16_t)b, (uint16_t)c);
        state("PD PROF", ok, millis() - t0);
    } else if (sscanf(cmd, "PD HOLD %ld", &a) == 1) {
        while (millis() - t0 < (uint32_t)a) {
            USBPD.maintain();
        }
        state("PD HOLD", 1, millis() - t0);
    } else if (!strcmp(cmd, "PD END")) {
        USBPD.end();
        state("PD END", 1, millis() - t0);
    } else {
        tc_unknown(cmd);
    }
}
