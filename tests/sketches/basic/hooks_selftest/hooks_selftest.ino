/* The Arduino hooks a sketch is allowed to override.
 *
 * Wiring: none, but the serialEvent() check needs the host to send a line, so
 * this sketch has one command beyond RUN. See tests/TEST_PLAN.ja.md.
 *
 *   RUN     initVariant / yield, then "hooks_selftest send a line now"
 *   <any>   after that: any line at all, delivered to serialEvent()
 *
 * The second step is why loop() stops calling tc_ready() while it waits.
 * main() runs loop() and then serialEventRun(), so a poll that drained the
 * buffer would consume the very line serialEvent() is supposed to see - the
 * hook would look broken when it is the test that ate the input.
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
#include "testcmd.h"

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

/* millis() at which the wait for a line gives up, or 0 when not waiting.
 * A hook that never fires has to fail rather than hang: silence is the one
 * answer a host cannot tell apart from a dead board. */
static uint32_t waiting_until;

static void run_checks()
{
    tc_check("initVariant_called", saw_init_variant);

    /* delay() calls yield() on every turn of its wait loop. */
    const uint32_t before = yields;
    delay(5);
    tc_check("yield_called", yields > before);

    saw_serial_event = false;
    waiting_until = millis() + 15000;
    Serial.println("hooks_selftest send a line now");
}

void setup()
{
    tc_begin("hooks_selftest");
}

void loop()
{
    if (waiting_until) {
        /* Deliberately no tc_ready() here - see the header comment. */
        if (saw_serial_event) {
            waiting_until = 0;
            tc_check("serialEvent_called", true);
            tc_done();
        } else if ((int32_t)(millis() - waiting_until) >= 0) {
            waiting_until = 0;
            tc_check("serialEvent_called", false);
            tc_done();
        }
        return;
    }

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
