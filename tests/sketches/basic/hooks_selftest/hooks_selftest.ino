/* The Arduino hooks a sketch is allowed to override.
 *
 * Wiring: none. The serialEvent() check needs the test to send a line, so it
 * only runs under pytest - the listen-only smoke runner skips this sketch.
 *
 * All four of these were broken at some point:
 *   - initVariant() was declared by the API and never called
 *   - yield() was not weak, so overriding it failed to link
 *   - serialEvent() was never dispatched, because main() did not call
 *     serialEventRun()
 *
 * Overriding them here is itself half the test: if any of them stops being a
 * weak symbol, this sketch fails to link.
 */
static volatile bool saw_init_variant;
static volatile uint32_t yields;
static volatile bool saw_serial_event;

void initVariant(void)
{
    saw_init_variant = true;
}

void yield(void)
{
    yields++;
}

void serialEvent(void)
{
    while (Serial.available()) {
        (void)Serial.read();
    }
    saw_serial_event = true;
}

static int failures;

static void check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        failures++;
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    delay(50);
    Serial.println("hooks_selftest start");

    check("initVariant_called", saw_init_variant);

    /* delay() calls yield() on every turn of its wait loop. */
    const uint32_t before = yields;
    delay(5);
    check("yield_called", yields > before);

    Serial.println("hooks_selftest send a line now");
}

void loop()
{
    static bool reported;
    if (saw_serial_event && !reported) {
        reported = true;
        check("serialEvent_called", true);
        Serial.print("hooks_selftest done failures=");
        Serial.println(failures);
    }
}
