/* The Arduino hooks a sketch is allowed to override.
 *
 * The serialEvent check needs a UART and a host that writes to it, so the host
 * names one first ("UART <n> <route> <baud>", see testcmd.h). Without one that
 * check is skipped and the rest still run.
 *
 *   RUN     initVariant / yield, then "hooks_selftest send a line now"
 *   <any>   after that, on the named UART: any line at all
 *
 * main() calls serialEventRun() after every loop(), which calls serialEvent()
 * for the monitor port and serialEventN() for SerialN. The named UART need not
 * be the monitor, so every hook is overridden here and the one for the named
 * port is the one that has to fire.
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

/* Every serialEvent hook lands here with its own port. Only the named UART's
 * counts, and only while this sketch is waiting for the line - a hook that
 * drained unconditionally would be testing nothing in particular. */
static void on_serial_event(arduino::CH32HardwareSerial &port)
{
    if (!waiting_until || &port != tc_uart()) {
        return;
    }
    /* Consume up to and including the newline, and only then report. Bytes
     * arrive a few at a time, so stopping at "no more available" would leave
     * the tail of the line behind. */
    while (port.available()) {
        if ((char)port.read() == '\n') {
            saw_serial_event = true;
        }
    }
}

#if defined(SERIAL_PORT_MONITOR)
void serialEvent(void) { on_serial_event(SERIAL_PORT_MONITOR); }
#endif
#if defined(CH32_SERIAL1_TX)
void serialEvent1(void) { on_serial_event(Serial1); }
#endif
#if defined(CH32_SERIAL2_TX)
void serialEvent2(void) { on_serial_event(Serial2); }
#endif
#if defined(CH32_SERIAL3_TX)
void serialEvent3(void) { on_serial_event(Serial3); }
#endif
#if defined(CH32_SERIAL4_TX)
void serialEvent4(void) { on_serial_event(Serial4); }
#endif
#if defined(CH32_SERIAL5_TX)
void serialEvent5(void) { on_serial_event(Serial5); }
#endif

static void run_checks()
{
    tc_check("initVariant_called", saw_init_variant);

    /* delay() calls yield() on every turn of its wait loop. */
    const uint32_t before = yields;
    delay(5);
    tc_check("yield_called", yields > before);

    if (!tc_uart()) {
        tc_skip("serialEvent_called", "no UART named");
        tc_done();
        return;
    }
    saw_serial_event = false;
    waiting_until = millis() + 15000;
    Console.println("hooks_selftest send a line now");
}

void setup()
{
    tc_begin("hooks_selftest");
}

void loop()
{
    if (waiting_until) {
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
    if (tc_uart_command(cmd)) {
        return;
    }
    if (!strcmp(cmd, "RUN")) {
        run_checks();
    } else {
        tc_unknown(cmd);
    }
}
