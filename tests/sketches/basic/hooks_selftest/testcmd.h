/* testcmd.h - the command protocol every hardware test sketch speaks.
 *
 * THIS FILE IS DISTRIBUTED. The original is tests/sketches/testcmd.h and a copy
 * lives in every case directory, because arduino-cli only compiles files that
 * are inside the sketch folder. Edit the original and run
 *
 *     uv run tests/sketches/sync_testcmd.py
 *
 * to push it out; `--check` fails when a copy has drifted, and
 * tests/generated/test_generated.py runs that check.
 *
 * ---------------------------------------------------------------------------
 *
 * The board repeats its banner and waits to be asked. Same shape the other
 * projects under ~/dev use, for the same reason: the flashing tool resets the
 * board and the console is opened after that, so anything printed in the first
 * moments of setup() is gone before anyone is listening.
 *
 *     setup()   Serial.begin() and nothing else
 *     loop()    tc_ready() -> prints "<name> READY" every half second,
 *               and returns the command line when one arrives
 *
 * Repeating is what makes it reliable rather than merely likely. A banner
 * printed once is a broadcast at a moment nobody can predict; a banner printed
 * every 500 ms is something the host can wait for whenever it happens to be
 * ready. On CH32V103 the once-only version put all nine sketches on the
 * *previous* sketch's output (heap_string was scored against core_api's lines),
 * and it was misdiagnosed three times - as wiring, as probe firmware, as a
 * flaky probe - before the cause was found.
 *
 * The other half of the reason is that setup() is the wrong place for work: a
 * check that takes twenty seconds looks exactly like a board that never booted.
 * Between RUN and the done line it looks like a board that is busy.
 *
 * Commands are lines, not single characters, because two sketches need an
 * argument (serial_echo) and because a stray byte as the flashing tool releases
 * the line cannot accidentally form one. PING is answered here so that every
 * sketch has a liveness check without having to remember to write one.
 *
 * ---------------------------------------------------------------------------
 *
 * No String, no std::string, no dynamic allocation: CH32V003 has 2 KB of RAM
 * and these sketches have to fit it. The command buffer is one fixed array.
 */
#ifndef TESTCMD_H
#define TESTCMD_H

#include <Arduino.h>
#include <string.h>

/* Longest command line accepted, terminator included. Overridable before the
 * include for a sketch whose commands carry a payload. */
#ifndef TC_CMD_MAX
#define TC_CMD_MAX 64
#endif

/* How often the banner is repeated while nothing has been asked. */
#ifndef TC_READY_MS
#define TC_READY_MS 500
#endif

/* `unused` rather than `inline`, so -Wall stays quiet about the helpers a given
 * sketch does not call while GCC keeps its own say over inlining. Forcing that
 * either way is worse: measured on the tightest board (core_api on CH32V003),
 * noinline cost 92 bytes over letting -Os choose, and choosing is free. */
#define TC_FN static __attribute__((unused))

static const char *tc_name_ = "test";
static int tc_failures_;

/* Minimal init. The banner is not printed here - see tc_ready(). */
TC_FN void tc_begin(const char *name)
{
    tc_name_ = name;
    tc_failures_ = 0;
    Serial.begin(115200);
}

/* One check, reported as "<name> PASS" or "<name> FAIL". */
TC_FN void tc_check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        tc_failures_++;
    }
}

/* The same, with the measured value on the FAIL line - "<name> FAIL 118".
 * A bare FAIL says which check broke; this says how far off it was. */
TC_FN void tc_checkv(const char *name, bool ok, long detail)
{
    if (ok) {
        Serial.print(name);
        Serial.println(" PASS");
        return;
    }
    Serial.print(name);
    Serial.print(" FAIL ");
    Serial.println(detail);
    tc_failures_++;
}

/* A check this part cannot run, and why. Not a failure: whether a series has a
 * second USART route or a spare timer is a property of the board. */
TC_FN void tc_skip(const char *name, const char *why)
{
    Serial.print(name);
    Serial.print(" SKIP ");
    Serial.println(why);
}

/* End of a RUN: "<name> done failures=0" is what the host waits for. */
TC_FN void tc_done(void)
{
    Serial.print(tc_name_);
    Serial.print(" done failures=");
    Serial.println(tc_failures_);
    tc_failures_ = 0;      /* a second RUN counts its own failures */
}

/* A command the sketch does not implement. Answered rather than ignored, so a
 * host that is out of step learns it immediately instead of timing out. */
TC_FN void tc_unknown(const char *cmd)
{
    Serial.print(tc_name_);
    Serial.print(" unknown cmd=");
    Serial.println(cmd);
}

/* Announce readiness, and return the next command line, or NULL.
 *
 * This is the whole of loop() for most sketches:
 *
 *     void loop() {
 *         const char *cmd = tc_ready();
 *         if (!cmd) return;
 *         if (!strcmp(cmd, "RUN")) run_checks();
 *         else tc_unknown(cmd);
 *     }
 *
 * The returned pointer is into a static buffer that the next call overwrites,
 * which is the point: no allocation, and one buffer per sketch.
 *
 * The banner resumes after a command has been served, so a second test can ask
 * again on a port it has just opened. A sketch that must stop announcing itself
 * for a while - hooks_selftest, which has to leave the input for serialEvent()
 * - simply does not call this until it is done waiting.
 */
TC_FN const char *tc_ready(void)
{
    static char buf[TC_CMD_MAX];
    static uint8_t len;
    static uint32_t next_banner;

    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            /* A line longer than the buffer keeps its head and drops the tail;
             * it then fails to match any command and comes back through
             * tc_unknown(), which is a visible answer rather than silence. */
            if (len < sizeof buf - 1) {
                buf[len++] = c;
            }
            continue;
        }
        buf[len] = '\0';
        len = 0;
        if (buf[0] == '\0') {
            continue;                       /* a blank line is not a command */
        }
        if (!strncmp(buf, "PING", 4) && (buf[4] == '\0' || buf[4] == ' ')) {
            /* "PING" -> "PONG", "PING 3" -> "PONG 3". Echoing the rest of the
             * line lets a caller that runs sketches back to back tell this
             * answer from one the last sketch left in the probe's FIFO. */
            Serial.print("PONG");
            Serial.println(buf + 4);
            continue;
        }
        return buf;
    }

    const uint32_t now = millis();
    if ((int32_t)(now - next_banner) >= 0) {
        next_banner = now + TC_READY_MS;
        Serial.print(tc_name_);
        Serial.println(" READY");
    }
    return NULL;
}

#endif /* TESTCMD_H */
