/* CH32.restart(), CH32.resetReason() and the watchdog, across real resets.
 *
 * The only honest way to test a reset is to cause one and look at the far
 * side, so this sketch is driven through three commands and survives two
 * reboots in one test run:
 *
 *   RUN      report this boot's reset reason, prove it is stable, and - where
 *            the family's LSI frequency is known - arm the watchdog and prove
 *            that feeding it keeps the sketch alive
 *   REBOOT   CH32.restart(); the host expects the banner to come back and the
 *            next RUN to say reset_reason=software
 *   BITE     stop feeding a short watchdog and go silent; the reset brings
 *            the banner back and the next RUN says reset_reason=watchdog.
 *            Where the watchdog is unavailable (no F_LSI in the device data:
 *            X033/X035 today) this substitutes CH32.restart(), so the flow
 *            stays linear and the output says which one happened.
 *
 * After RUN has armed the watchdog, loop() feeds it forever - the IWDG
 * cannot be stopped, so the sketch must keep it fed to keep answering.
 * Feeding an unarmed watchdog is a harmless key write.
 */
#include <CH32.h>

#include "testcmd.h"

static volatile bool starving = false;

static void run_checks()
{
    /* First line on purpose: the host asserts this value differently on the
     * first boot (anything), after REBOOT (software) and after BITE
     * (watchdog, or software where the watchdog is unavailable). */
    Serial.print("reset_reason=");
    Serial.println(CH32.resetReasonName());

    tc_check("reason_stable", CH32.resetReason() == CH32.resetReason()
                              && CH32.resetReasonName()[0] != '\0');

    const bool enabled = CH32.wdtEnable(300);
#if defined(CH32_LSI_HZ) && defined(CH32_IWDG_BASE)
    tc_check("wdt_enable_honest", enabled);
    /* Four timeouts' worth of staying alive while fed is the proof that
     * feeding works; the bite is proven later, across the reset. */
    const uint32_t t0 = millis();
    uint32_t last_feed = t0;
    while (millis() - t0 < 1200u) {
        if (millis() - last_feed >= 50u) {
            last_feed = millis();
            CH32.wdtFeed();
        }
        tc_tick();                    /* keep the bridge moving meanwhile */
    }
    tc_check("wdt_survives_fed", true);
#else
    tc_check("wdt_enable_honest", !enabled);
    tc_skip("wdt_survives_fed", "no IWDG, or no F_LSI in the device data");
#endif

    tc_done();
}

void setup()
{
    tc_begin("system_selftest");
}

void loop()
{
    if (starving) {
        /* Deliberately silent: the next line the host sees after "biting"
         * is the new boot's banner, which is what makes the flow
         * deterministic. The watchdog does the rest. */
        return;
    }
    CH32.wdtFeed();

    const char *cmd = tc_ready();
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "RUN")) {
        run_checks();
    } else if (!strcmp(cmd, "REBOOT")) {
        Serial.println("rebooting");
        Serial.flush();
        CH32.restart();
    } else if (!strcmp(cmd, "BITE")) {
        const bool armed = CH32.wdtEnable(100);
        Serial.println("biting");
        Serial.flush();
        if (armed) {
            starving = true;          /* silence; the watchdog ends this */
        } else {
            CH32.restart();           /* substitute, and the reason says so */
        }
    } else {
        tc_unknown(cmd);
    }
}
