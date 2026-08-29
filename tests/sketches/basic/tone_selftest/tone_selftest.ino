/* tone() with no speaker and no wiring.
 *
 * A pin driven as an output still reads back its own level, so the sketch can
 * watch the pad it is sounding on: "the tone is playing" becomes "the pin
 * changes", and "the duration expired" becomes "it stopped changing and is
 * low". That covers everything except the sound itself.
 *
 * The frequency is checked by counting edges over a known interval. Polling
 * cannot see every edge, so the count is only used to tell 500 Hz from a
 * timer left at some unrelated rate, not to measure accuracy.
 */
#include "testcmd.h"

static const uint8_t PIN = PA1;

/* True as soon as the pad changes level, false if it held still for ms. */
static bool toggles_within(uint32_t ms)
{
    const int first = digitalRead(PIN);
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        if (digitalRead(PIN) != first) {
            return true;
        }
    }
    return false;
}

static uint32_t count_edges(uint32_t ms)
{
    uint32_t edges = 0;
    int last = digitalRead(PIN);
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        const int now = digitalRead(PIN);
        if (now != last) {
            edges++;
            last = now;
        }
    }
    return edges;
}

static void run_checks()
{
#ifdef CH32_TONE_TIMER
    /* 1. It makes the pin move. */
    tone(PIN, 500);
    tc_check("tone_toggles_pin", toggles_within(20));

    /* 2. At roughly the right rate: 500 Hz is 1000 edges a second, so 200 ms
     *    holds about 200. Anything from a quarter to four times that means the
     *    timer is running at some other rate entirely. */
    const uint32_t edges = count_edges(200);
    tc_check("tone_rate_plausible", edges > 50 && edges < 800);

    /* 3. A second tone on a pin that does not exist must not disturb it. */
    tone(0xFE, 1000);
    tc_check("invalid_pin_ignored", toggles_within(20));

    /* 4. noTone() stops it and leaves the pad low. */
    noTone(PIN);
    tc_check("notone_stops", !toggles_within(20));
    tc_check("notone_leaves_low", digitalRead(PIN) == LOW);

    /* 5. A duration stops it on its own. */
    tone(PIN, 500, 50);
    tc_check("duration_plays", toggles_within(20));
    delay(120);
    tc_check("duration_stops", !toggles_within(20));
    tc_check("duration_leaves_low", digitalRead(PIN) == LOW);

    /* 6. And it can be started again afterwards. */
    tone(PIN, 800);
    tc_check("restart_plays", toggles_within(20));
    noTone(PIN);
#else
    static const char *const WHY = "no timer for tone on this series";
    tc_skip("tone_toggles_pin", WHY);
    tc_skip("tone_rate_plausible", WHY);
    tc_skip("invalid_pin_ignored", WHY);
    tc_skip("notone_stops", WHY);
    tc_skip("notone_leaves_low", WHY);
    tc_skip("duration_plays", WHY);
    tc_skip("duration_stops", WHY);
    tc_skip("duration_leaves_low", WHY);
    tc_skip("restart_plays", WHY);
#endif

    tc_done();
}

void setup()
{
    tc_begin("tone_selftest");
}

void loop()
{
    const char *cmd = tc_ready();
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "RUN")) {
        run_checks();
    } else {
        tc_unknown(cmd);
    }
}
