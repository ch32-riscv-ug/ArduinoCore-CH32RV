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
static const uint8_t PIN = LED_BUILTIN;

static int failures;

static void check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        failures++;
    }
}

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

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    delay(50);
    Serial.println("tone_selftest start");

#ifdef CH32_TONE_TIMER
    /* 1. It makes the pin move. */
    tone(PIN, 500);
    check("tone_toggles_pin", toggles_within(20));

    /* 2. At roughly the right rate: 500 Hz is 1000 edges a second, so 200 ms
     *    holds about 200. Anything from a quarter to four times that means the
     *    timer is running at some other rate entirely. */
    const uint32_t edges = count_edges(200);
    check("tone_rate_plausible", edges > 50 && edges < 800);

    /* 3. A second tone on a pin that does not exist must not disturb it. */
    tone(0xFE, 1000);
    check("invalid_pin_ignored", toggles_within(20));

    /* 4. noTone() stops it and leaves the pad low. */
    noTone(PIN);
    check("notone_stops", !toggles_within(20));
    check("notone_leaves_low", digitalRead(PIN) == LOW);

    /* 5. A duration stops it on its own. */
    tone(PIN, 500, 50);
    check("duration_plays", toggles_within(20));
    delay(120);
    check("duration_stops", !toggles_within(20));
    check("duration_leaves_low", digitalRead(PIN) == LOW);

    /* 6. And it can be started again afterwards. */
    tone(PIN, 800);
    check("restart_plays", toggles_within(20));
    noTone(PIN);
#else
    Serial.println("tone_toggles_pin SKIP no timer for tone on this series");
    Serial.println("tone_rate_plausible SKIP no timer for tone on this series");
    Serial.println("invalid_pin_ignored SKIP no timer for tone on this series");
    Serial.println("notone_stops SKIP no timer for tone on this series");
    Serial.println("notone_leaves_low SKIP no timer for tone on this series");
    Serial.println("duration_plays SKIP no timer for tone on this series");
    Serial.println("duration_stops SKIP no timer for tone on this series");
    Serial.println("duration_leaves_low SKIP no timer for tone on this series");
    Serial.println("restart_plays SKIP no timer for tone on this series");
#endif

    Serial.print("tone_selftest done failures=");
    Serial.println(failures);
}

void loop()
{
}
