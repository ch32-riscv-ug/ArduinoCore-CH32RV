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
 * **The harness talks over `Console`, which is the debug module's data
 * registers (SerialDMSeq, the dmseq framing), not a UART.** The UART is one of the things under
 * test: its pins differ per part and per jig, and a sketch testing it moves it
 * around. So it cannot also be how the host and the board find each other.
 * Console needs no pin and is up the moment the core runs; the host reads it
 * through the debug probe (ch32rv `monitor --source dmseq` on a WCH-Link,
 * `target.console` framing 2 on an OEP probe). dmseq rather than SerialDMDATA's
 * minichlink framing because the harness must not lose or double a byte: over a
 * link where a DMI access can go astray, framing 1 did both (oep-spec
 * docs/target-console-dmseq.ja.md). In a sketch, `Console` is the harness and
 * `Serial` only ever means a UART that is being tested - unless the sketch
 * names a UART as its console in tc_begin(), for a host that needs the debug
 * link for something else at the same time (see Console below).
 *
 * A sketch that tests a UART does not pick its pins. The host knows which of
 * them reach something it can listen on, so it says so -
 * "UART <n> <route> <baud>" - and the sketch hands the line to
 * tc_uart_command(), which brings SerialN up on that route and answers on
 * Console. Until then tc_uart() is NULL and the UART checks report SKIP.
 *
 * The board repeats its banner and waits to be asked. Same shape the other
 * projects under ~/dev use, for the same reason: the flashing tool resets the
 * board and the host attaches after that, and a Console write with nobody
 * collecting gives up and is dropped - so anything printed once in the first
 * moments of setup() is gone before anyone is listening.
 *
 *     setup()   tc_begin() and nothing else
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
#include <SerialDMSeq.h>
#include <ch32_clock.h>
#include <ch32_registers.h>
#include <stdlib.h>
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

/* Where the harness talks: the debug module's console unless the sketch says otherwise.
 *
 * The exception is a sketch whose host uses the debug link for something else while the
 * sketch runs - reading registers from outside, say. Its console cannot be that same link,
 * so it names the UART it talks over instead, and for that sketch the UART is plumbing
 * rather than a thing under test:
 *
 *     tc_begin("reg_probe", Serial);
 *
 * A small forwarder rather than a reference, so the choice is made at run time by
 * tc_begin() and every tc_* function and every Console.print() follows it. */
class TcConsole : public Stream {
public:
    arduino::HardwareSerial *port = &SerialDMSeq;
    void begin(unsigned long baud) { port->begin(baud); }
    size_t write(uint8_t c) override { return port->write(c); }
    size_t write(const uint8_t *b, size_t n) override { return port->write(b, n); }
    int available() override { return port->available(); }
    int read() override { return port->read(); }
    int peek() override { return port->peek(); }
    void flush() override { port->flush(); }
    using Print::write;
};
static TcConsole Console;

/* Minimal init. The banner is not printed here - see tc_ready(). */
TC_FN void tc_begin(const char *name, arduino::HardwareSerial &console = SerialDMSeq)
{
    tc_name_ = name;
    tc_failures_ = 0;
    Console.port = &console;
    Console.begin(115200);
}

/* One check, reported as "<name> PASS" or "<name> FAIL". */
TC_FN void tc_check(const char *name, bool ok)
{
    Console.print(name);
    Console.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        tc_failures_++;
    }
}

/* The same, with the measured value on the FAIL line - "<name> FAIL 118".
 * A bare FAIL says which check broke; this says how far off it was. */
TC_FN void tc_checkv(const char *name, bool ok, long detail)
{
    if (ok) {
        Console.print(name);
        Console.println(" PASS");
        return;
    }
    Console.print(name);
    Console.print(" FAIL ");
    Console.println(detail);
    tc_failures_++;
}

/* A check this part cannot run, and why. Not a failure: whether a series has a
 * second USART route or a spare timer is a property of the board. */
TC_FN void tc_skip(const char *name, const char *why)
{
    Console.print(name);
    Console.print(" SKIP ");
    Console.println(why);
}

/* End of a RUN: "<name> done failures=0" is what the host waits for. */
TC_FN void tc_done(void)
{
    Console.print(tc_name_);
    Console.print(" done failures=");
    Console.println(tc_failures_);
    tc_failures_ = 0;      /* a second RUN counts its own failures */
}

/* A command the sketch does not implement. Answered rather than ignored, so a
 * host that is out of step learns it immediately instead of timing out. */
TC_FN void tc_unknown(const char *cmd)
{
    Console.print(tc_name_);
    Console.print(" unknown cmd=");
    Console.println(cmd);
}

/* Print the banner if it is due, and read nothing - for a sketch that has to
 * keep announcing itself while it leaves Console's input alone for a moment.
 */
TC_FN void tc_tick(void)
{
    static uint32_t next_banner;

    const uint32_t now = millis();
    if ((int32_t)(now - next_banner) >= 0) {
        next_banner = now + TC_READY_MS;
        Console.print(tc_name_);
        Console.println(" READY");
    }
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
 * The banner keeps going after a command has been served, so a host that
 * attaches late, or lost its place, can always find the board again. It costs
 * nothing when nobody is listening: a Console write that finds no host gives
 * up once and stays cheap until one attaches.
 */
/* Put the clock back if the debug probe moved it. A WCH-LinkE rewrites a
 * running target's RCC_CFGR0 (and FLASH ACTLR) when it attaches - measured on
 * CH32V307, V203 and L103, not V003, and it is the probe firmware, not the
 * tool (memory of 2026-09-05). Console is read through that attach, so by the
 * time a command line arrives it has happened, once per session: re-running
 * SystemInit() here gives UART baud rates and timing checks the clock the
 * core chose. Same test as tests/manual/reg_probe. */
static uint32_t tc_clock_heals;

TC_FN bool tc_clock_is_ours(void)
{
    const uint32_t cfgr0 = CH32_RCC_CFGR0;
    const uint32_t sw = CH32_CLOCK_USE_PLL ? CH32_RCC_CFGR0_SW_PLL : CH32_RCC_CFGR0_SW_HSI;
    if ((cfgr0 & CH32_RCC_CFGR0_SWS_MASK) != (sw << 2)) {
        return false;
    }
    if ((cfgr0 & (CH32_RCC_CFGR0_HPRE_MASK | CH32_RCC_CFGR0_PPRE1_MASK |
                  CH32_RCC_CFGR0_PPRE2_MASK)) != CH32_RCC_CFGR0_HPRE(CH32_HPRE_FIELD)) {
        return false;
    }
#if CH32_CLOCK_USE_PLL
    if ((cfgr0 & (uint32_t)CH32_CLOCK_PLL_MASK) != (uint32_t)CH32_CLOCK_PLL_VALUE) {
        return false;
    }
#endif
    return true;
}

TC_FN void tc_heal_clock(void)
{
    if (!tc_clock_is_ours()) {
        SystemInit();
        tc_clock_heals++;
    }
}

TC_FN const char *tc_ready(void)
{
    static char buf[TC_CMD_MAX];
    static uint8_t len;

    while (Console.available()) {
        const char c = (char)Console.read();
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
        tc_heal_clock();
        if (buf[0] == '\0') {
            continue;                       /* a blank line is not a command */
        }
        if (!strncmp(buf, "PING", 4) && (buf[4] == '\0' || buf[4] == ' ')) {
            /* "PING" -> "PONG", "PING 3" -> "PONG 3". Echoing the rest of the
             * line lets a caller that runs sketches back to back tell this
             * answer from one the last sketch left in the probe's FIFO. */
            Console.print("PONG");
            Console.println(buf + 4);
            continue;
        }
        return buf;
    }

    tc_tick();
    return NULL;
}

/* ------------------------------------------------------------------ UART */

/* The UART under test, once the host has named one; NULL until then. */
static arduino::CH32HardwareSerial *tc_uart_;

TC_FN arduino::CH32HardwareSerial *tc_uart(void)
{
    return tc_uart_;
}

TC_FN arduino::CH32HardwareSerial *tc_serial_port(long n)
{
    switch (n) {
#if defined(CH32_SERIAL1_TX)
    case 1: return &Serial1;
#endif
#if defined(CH32_SERIAL2_TX)
    case 2: return &Serial2;
#endif
#if defined(CH32_SERIAL3_TX)
    case 3: return &Serial3;
#endif
#if defined(CH32_SERIAL4_TX)
    case 4: return &Serial4;
#endif
#if defined(CH32_SERIAL5_TX)
    case 5: return &Serial5;
#endif
    default: return NULL;
    }
}

/* "UART <n> <route> <baud>": bring SerialN up on that route. Returns true when
 * the line was this command, whether or not it worked - the answer on Console
 * says which. Only a sketch that tests a UART calls this, so the others never
 * link the UART's begin() and route code on a part that has no room for it.
 *
 *     UART OK 1 1 115200
 *     UART FAIL no-port 7
 */
TC_FN bool tc_uart_command(const char *cmd)
{
    if (strncmp(cmd, "UART ", 5) != 0) {
        return false;
    }
    char *end;
    const long n = strtol(cmd + 5, &end, 10);
    const long route = strtol(end, &end, 10);
    const long baud = strtol(end, &end, 10);
    arduino::CH32HardwareSerial *port = tc_serial_port(n);
    if (!port) {
        Console.print("UART FAIL no-port ");
        Console.println(n);
        return true;
    }
    if (!port->setRoute((uint8_t)route)) {
        Console.print("UART FAIL no-route ");
        Console.println(route);
        return true;
    }
    if (baud <= 0) {
        Console.println("UART FAIL baud");
        return true;
    }
    port->begin((unsigned long)baud);
    tc_uart_ = port;
    Console.print("UART OK ");
    Console.print(n);
    Console.print(' ');
    Console.print(route);
    Console.print(' ');
    Console.println(baud);
    return true;
}

#endif /* TESTCMD_H */
