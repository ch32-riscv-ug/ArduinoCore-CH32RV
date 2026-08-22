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

/* millis() at which the wait for a line gives up, or 0 when not waiting.
 * A hook that never fires has to fail rather than hang: silence is the one
 * answer a host cannot tell apart from a dead board. It is also what keeps
 * serialEvent() out of the command reader's way - see below. */
static uint32_t waiting_until;

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
    /* Only while this sketch is expecting the line, and not a moment before.
     *
     * main() calls serialEventRun() after every loop(), so a hook that drains
     * unconditionally competes with tc_ready() for the same bytes - and wins
     * whenever they land between the two calls. Measured on CH32V103: the
     * host's PING disappeared into here and the run failed as "banner but no
     * PONG", on a board whose RX was working perfectly.
     */
    if (!waiting_until) {
        return;
    }
    /* Consume up to and including the newline, and only then report. Bytes
     * arrive a few at a time, so a hook that stopped at "no more available"
     * would leave the tail of the line for tc_ready() to read back as an
     * unknown command. */
    while (Serial.available()) {
        if ((char)Serial.read() == '\n') {
            saw_serial_event = true;
        }
    }
}

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
        /* tc_tick() and not tc_ready(): the banner has to keep going or the
         * bridge stalls with the last partial line inside it, but reading
         * would consume the line serialEvent() is waiting for. */
        tc_tick();
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
