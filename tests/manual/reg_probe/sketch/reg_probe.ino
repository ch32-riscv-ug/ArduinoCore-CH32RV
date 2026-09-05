/* A remote control for the core API, so that the debugger can be the judge.
 *
 * Every other sketch on this bench decides pass/fail on the target. This one
 * decides nothing: reg_probe.py tells it which API to call, it calls it and
 * says OK, and the host then reads the peripheral registers through the
 * WCH-Link and compares them with what device-data says they should hold.
 * That is TEST_PLAN's "method 3", and it is the only method that can see an
 * AFIO remap field, a pull-up select bit or a timer prescaler without a wire
 * or an instrument - and without trusting the core's own register map, which
 * is exactly the thing under test.
 *
 * Commands are one line each. Pins arrive as the port-encoded numbers the
 * core uses (PA1 = 1, PB12 = 44 ...), because the host has the variant header
 * and this sketch should not carry a second copy of the pad table.
 *
 *   PINMODE <pin> IN|PU|PD|OUT|OD        pinMode()
 *   DWRITE <pin> 0|1                     digitalWrite()
 *   DREAD <pin>                          -> VAL <0|1>
 *   AWRITE <pin> <value>                 analogWrite()
 *   AREAD <pin>                          -> VAL <n>
 *   SERIAL <n> BEGIN [baud]              Serial<n>.begin()  (refused for the monitor)
 *   SERIAL <n> END
 *   SERIAL <n> ROUTE <r>                 -> VAL <0|1>  setRoute()
 *   SERIAL <n> PINS <tx> <rx>            -> VAL <0|1>  setPins()
 *   SERIAL <n> EXCURSION <r> <back> <ms> OK first, then setRoute(r), wait,
 *                                        setRoute(back), then DONE <went> <back>
 *                                        - the way to move the monitor itself
 *   EXTI <pin> RISING|FALLING|CHANGE|LOW attachInterrupt()
 *   EXTI <pin> DETACH                    detachInterrupt()
 *   TONE <pin> <hz>  /  NOTONE <pin>
 *   WIRE BEGIN | END | CLOCK <hz> | ROUTE <r> | PINS <scl> <sda>
 *   SPI  BEGIN | END | ROUTE <r> | PINS <sck> <miso> <mosi>
 *        | SETTINGS <hz> <mode> <order>  (beginTransaction/endTransaction)
 *
 *   CLOCK                                -> RCC <CTLR> <CFGR0> (hex), VAL <heals>
 *   PEEK <addr>                          -> VAL <word>   a 32-bit read by the core
 *
 * Every command ends with "OK <verb>" or "ERR <verb> <why>", so the host can
 * wait for the verb and never confuses the answer with the banner.
 *
 * CLOCK and PEEK exist because of the probe. Measured on this bench (CH32V307,
 * WCH-LinkE 2.22, probe-rs 0.32 and ch32rv 0.5 alike): every attach rewrites
 * the target's RCC_CFGR0 - PLL x15 and APB1/2 instead of the x12 the core set
 * - so the part runs on at 120 MHz and the console comes out at the wrong
 * baud. The core keeps running; only the clock moved. So RCC itself can only
 * be read from inside (the probe's own view of RCC is the probe's doing), and
 * loop() puts the clock back with SystemInit() whenever it finds it moved -
 * which is also a live test of A-11, SystemInit from an arbitrary RCC state.
 *
 * No String, no printf: CH32V003 has 2 KB of RAM and 16 KB of flash and this
 * sketch has to fit beside Wire and SPI.
 */
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <ch32_clock.h>
#include <ch32_registers.h>

#include "testcmd.h"

static volatile uint32_t exti_hits;
static void on_edge() { exti_hits++; }

static uint32_t heals;

/* Is RCC still the way SystemInit left it? Source, prescalers, PLL word. */
static bool clock_is_ours()
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

static void heal_clock()
{
    if (!clock_is_ours()) {
        SystemInit();
        heals++;
    }
}

static void ok(const char *verb)
{
    Serial.print("OK ");
    Serial.println(verb);
}

static void err(const char *verb, const char *why)
{
    Serial.print("ERR ");
    Serial.print(verb);
    Serial.print(' ');
    Serial.println(why);
}

static void val(long v)
{
    Serial.print("VAL ");
    Serial.println(v);
}

/* strtok_r on a copy: the buffer tc_ready() returns is reused for the next
 * command, and pointing into it while printing is fine, but not while the
 * next line is arriving. */
static char *tok(char **save)
{
    return strtok_r(NULL, " ", save);
}

static bool arg_long(char **save, long *out)
{
    const char *s = tok(save);
    if (!s) {
        return false;
    }
    char *end;
    *out = strtol(s, &end, 0);
    return *end == '\0';
}

static arduino::CH32HardwareSerial *serial_by_index(long n)
{
    switch (n) {
#ifdef CH32_SERIAL1_TX
    case 1: return &Serial1;
#endif
#ifdef CH32_SERIAL2_TX
    case 2: return &Serial2;
#endif
#ifdef CH32_SERIAL3_TX
    case 3: return &Serial3;
#endif
#ifdef CH32_SERIAL4_TX
    case 4: return &Serial4;
#endif
#ifdef CH32_SERIAL5_TX
    case 5: return &Serial5;
#endif
    default: return nullptr;
    }
}

static void do_pinmode(char **save)
{
    long pin;
    const char *mode;
    if (!arg_long(save, &pin) || !(mode = tok(save))) {
        err("PINMODE", "args");
        return;
    }
    if (!strcmp(mode, "IN"))       pinMode((pin_size_t)pin, INPUT);
    else if (!strcmp(mode, "PU"))  pinMode((pin_size_t)pin, INPUT_PULLUP);
    else if (!strcmp(mode, "PD"))  pinMode((pin_size_t)pin, INPUT_PULLDOWN);
    else if (!strcmp(mode, "OUT")) pinMode((pin_size_t)pin, OUTPUT);
    else if (!strcmp(mode, "OD"))  pinMode((pin_size_t)pin, OUTPUT_OPENDRAIN);
    else {
        err("PINMODE", "mode");
        return;
    }
    ok("PINMODE");
}

static void do_serial(char **save)
{
    long n;
    const char *sub;
    if (!arg_long(save, &n) || !(sub = tok(save))) {
        err("SERIAL", "args");
        return;
    }
    arduino::CH32HardwareSerial *port = serial_by_index(n);
    if (!port) {
        err("SERIAL", "no such port");
        return;
    }
    const bool monitor = (n == CH32_SERIAL_DEFAULT);

    if (!strcmp(sub, "BEGIN")) {
        if (monitor) {
            err("SERIAL", "monitor");   /* would restart the console */
            return;
        }
        long baud = 115200;
        arg_long(save, &baud);
        port->begin((unsigned long)baud);
        ok("SERIAL");
    } else if (!strcmp(sub, "END")) {
        if (monitor) {
            err("SERIAL", "monitor");
            return;
        }
        port->end();
        ok("SERIAL");
    } else if (!strcmp(sub, "ROUTE")) {
        long r;
        if (!arg_long(save, &r)) {
            err("SERIAL", "args");
            return;
        }
        if (monitor) {
            err("SERIAL", "monitor");   /* use EXCURSION */
            return;
        }
        val(port->setRoute((uint8_t)r));
        ok("SERIAL");
    } else if (!strcmp(sub, "PINS")) {
        long tx, rx;
        if (!arg_long(save, &tx) || !arg_long(save, &rx)) {
            err("SERIAL", "args");
            return;
        }
        if (monitor) {
            err("SERIAL", "monitor");
            return;
        }
        val(port->setPins((uint8_t)tx, (uint8_t)rx));
        ok("SERIAL");
    } else if (!strcmp(sub, "EXCURSION")) {
        long r, back, ms;
        if (!arg_long(save, &r) || !arg_long(save, &back) ||
            !arg_long(save, &ms)) {
            err("SERIAL", "args");
            return;
        }
        /* Acknowledge first and let it drain: once the monitor has moved,
         * nothing printed reaches the host until it is back. */
        ok("SERIAL");
        Serial.flush();
        const bool went = port->setRoute((uint8_t)r);
        delay((unsigned long)ms);
        /* The host read registers while we were away, so the probe has
         * moved the clock; put it back before anything is printed. */
        heal_clock();
        const bool came = port->setRoute((uint8_t)back);
        Serial.print("DONE ");
        Serial.print(went ? 1 : 0);
        Serial.print(' ');
        Serial.println(came ? 1 : 0);
    } else {
        err("SERIAL", "sub");
    }
}

static void do_exti(char **save)
{
    long pin;
    const char *mode;
    if (!arg_long(save, &pin) || !(mode = tok(save))) {
        err("EXTI", "args");
        return;
    }
    if (!strcmp(mode, "DETACH")) {
        detachInterrupt(digitalPinToInterrupt((pin_size_t)pin));
    } else {
        PinStatus m;
        if (!strcmp(mode, "RISING"))       m = RISING;
        else if (!strcmp(mode, "FALLING")) m = FALLING;
        else if (!strcmp(mode, "CHANGE"))  m = CHANGE;
        else if (!strcmp(mode, "LOW"))     m = LOW;
        else {
            err("EXTI", "mode");
            return;
        }
        attachInterrupt(digitalPinToInterrupt((pin_size_t)pin), on_edge, m);
    }
    ok("EXTI");
}

static void do_wire(char **save)
{
    const char *sub = tok(save);
    if (!sub) {
        err("WIRE", "args");
        return;
    }
    if (!strcmp(sub, "BEGIN")) {
        Wire.begin();
    } else if (!strcmp(sub, "END")) {
        Wire.end();
    } else if (!strcmp(sub, "CLOCK")) {
        long hz;
        if (!arg_long(save, &hz)) {
            err("WIRE", "args");
            return;
        }
        Wire.setClock((uint32_t)hz);
    } else if (!strcmp(sub, "ROUTE")) {
        long r;
        if (!arg_long(save, &r)) {
            err("WIRE", "args");
            return;
        }
        val(Wire.setRoute((uint8_t)r));
    } else if (!strcmp(sub, "PINS")) {
        long scl, sda;
        if (!arg_long(save, &scl) || !arg_long(save, &sda)) {
            err("WIRE", "args");
            return;
        }
        val(Wire.setPins((uint8_t)scl, (uint8_t)sda));
    } else {
        err("WIRE", "sub");
        return;
    }
    ok("WIRE");
}

static void do_spi(char **save)
{
    const char *sub = tok(save);
    if (!sub) {
        err("SPI", "args");
        return;
    }
    if (!strcmp(sub, "BEGIN")) {
        SPI.begin();
    } else if (!strcmp(sub, "END")) {
        SPI.end();
    } else if (!strcmp(sub, "ROUTE")) {
        long r;
        if (!arg_long(save, &r)) {
            err("SPI", "args");
            return;
        }
        val(SPI.setRoute((uint8_t)r));
    } else if (!strcmp(sub, "PINS")) {
        long sck, miso, mosi;
        if (!arg_long(save, &sck) || !arg_long(save, &miso) ||
            !arg_long(save, &mosi)) {
            err("SPI", "args");
            return;
        }
        val(SPI.setPins((uint8_t)sck, (uint8_t)miso, (uint8_t)mosi));
    } else if (!strcmp(sub, "SETTINGS")) {
        long hz, mode, order;
        if (!arg_long(save, &hz) || !arg_long(save, &mode) ||
            !arg_long(save, &order)) {
            err("SPI", "args");
            return;
        }
        SPI.beginTransaction(SPISettings((uint32_t)hz,
                                         order ? LSBFIRST : MSBFIRST,
                                         (uint8_t)mode));
        SPI.endTransaction();
    } else {
        err("SPI", "sub");
        return;
    }
    ok("SPI");
}

static void dispatch(const char *line)
{
    char buf[TC_CMD_MAX];
    strncpy(buf, line, sizeof buf - 1);
    buf[sizeof buf - 1] = '\0';

    char *save;
    const char *verb = strtok_r(buf, " ", &save);
    if (!verb) {
        return;
    }
    long pin, v;

    if (!strcmp(verb, "RUN")) {
        /* Nothing is checked on the target; the protocol still wants a done
         * line so the generic runners can drive this sketch too. */
        tc_done();
    } else if (!strcmp(verb, "PINMODE")) {
        do_pinmode(&save);
    } else if (!strcmp(verb, "DWRITE")) {
        if (!arg_long(&save, &pin) || !arg_long(&save, &v)) {
            err("DWRITE", "args");
            return;
        }
        digitalWrite((pin_size_t)pin, v ? HIGH : LOW);
        ok("DWRITE");
    } else if (!strcmp(verb, "DREAD")) {
        if (!arg_long(&save, &pin)) {
            err("DREAD", "args");
            return;
        }
        val(digitalRead((pin_size_t)pin) == HIGH ? 1 : 0);
        ok("DREAD");
    } else if (!strcmp(verb, "AWRITE")) {
        if (!arg_long(&save, &pin) || !arg_long(&save, &v)) {
            err("AWRITE", "args");
            return;
        }
        analogWrite((pin_size_t)pin, (int)v);
        ok("AWRITE");
    } else if (!strcmp(verb, "AREAD")) {
        if (!arg_long(&save, &pin)) {
            err("AREAD", "args");
            return;
        }
        val(analogRead((pin_size_t)pin));
        ok("AREAD");
    } else if (!strcmp(verb, "SERIAL")) {
        do_serial(&save);
    } else if (!strcmp(verb, "EXTI")) {
        do_exti(&save);
    } else if (!strcmp(verb, "TONE")) {
        if (!arg_long(&save, &pin) || !arg_long(&save, &v)) {
            err("TONE", "args");
            return;
        }
        tone((uint8_t)pin, (unsigned int)v);
        ok("TONE");
    } else if (!strcmp(verb, "NOTONE")) {
        if (!arg_long(&save, &pin)) {
            err("NOTONE", "args");
            return;
        }
        noTone((uint8_t)pin);
        ok("NOTONE");
    } else if (!strcmp(verb, "WIRE")) {
        do_wire(&save);
    } else if (!strcmp(verb, "SPI")) {
        do_spi(&save);
    } else if (!strcmp(verb, "CLOCK")) {
        Serial.print("RCC ");
        Serial.print((uint32_t)CH32_RCC_CTLR, HEX);
        Serial.print(' ');
        Serial.println((uint32_t)CH32_RCC_CFGR0, HEX);
        val((long)heals);
        ok("CLOCK");
    } else if (!strcmp(verb, "PEEK")) {
        if (!arg_long(&save, &v)) {
            err("PEEK", "args");
            return;
        }
        Serial.print("VAL ");
        Serial.println((uint32_t)CH32_REG32((uint32_t)v));
        ok("PEEK");
    } else {
        tc_unknown(line);
    }
}

void setup()
{
    tc_begin("reg_probe");
}

void loop()
{
    heal_clock();
    const char *cmd = tc_ready();
    if (cmd) {
        dispatch(cmd);
    }
}
