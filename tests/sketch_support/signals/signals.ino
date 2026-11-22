/* Signals on one line each (tests/instrumented/signals): analogWrite(),
 * Servo, tone(), an external interrupt and a timer's pulse capture, on a pad
 * the probe reaches (a `<port>@<pad>` line; the test picks it). Pins are the
 * sketch's numbers.
 *
 *   ready <nonce>          READY <nonce> <build id>
 *   restart                CH32RV.restart(): each test starts from a fresh
 *                          run (analogWrite() keeps its timer, so a pulse
 *                          capture after it on that timer is refused)
 *   caps <pin>             CAPS <pin> <timer> <channel>: the pad's PWM timer
 *                          and channel on the default route (0 0: none)
 *   pwm <pin> <value> <hz> PWM <ok>: analogWrite(pin, value) of 255 at hz
 *   pwmoff <pin>           PWMOFF: analogWrite(pin, 0)
 *   servo <pin> <us>       SERVO <ok>: a Servo on pin, writeMicroseconds(us)
 *   servooff               SERVOOFF
 *   tone <pin> <hz>        TONE: tone(pin, hz)
 *   toned <pin> <hz> <ms>  TONE: tone(pin, hz, ms), which stops by itself
 *   notone <pin>           NOTONE (the pin as noTone() leaves it)
 *   input <pin>            INPUT: pinMode(pin, INPUT), the line let go
 *   pulsein <pin> <state> <timeout us> <long>  PULSEIN <us>: pulseIn(), or
 *                          pulseInLong() when long is 1 (the pin an input)
 *   level <pin> <0/1>      LEVEL: digitalWrite(pin, v)
 *   irq <pin> <mode>       IRQ <ok>: attachInterrupt, mode 1 RISING, 2 FALLING,
 *                          3 CHANGE; the count starts at 0
 *   count                  COUNT <edges seen>
 *   irqoff <pin>           IRQOFF
 *   cap <pin>              CAP <ok>: CH32RVPulseCapture on pin (10 Hz up)
 *   capread                PULSE <period ticks> <high ticks> <tick Hz>, or
 *                          PULSE NONE
 *   capoff                 CAPOFF
 *   rgb <pin> <n>          RGB <n>: rgbLedWrite() n times in a row (LED i: red
 *                          0x12 + i, green 0xA5, blue 0x3C ^ i; GRB on the
 *                          line), then 300 us low for the latch
 *   rgbbuf <pin> <n>       RGB <n>: the same n LEDs (n <= 8) in one
 *                          ch32rv_rgb_led_write(): one frame
 *   rgbhw <pin>            RGBHW <1 / 0>: the two above go by the WS2812
 *                          block there (ch32rv_rgb_led_is_hardware())
 *
 * The timer features (timer_reply; V003's build leaves out what `has` does
 * not list):
 *   has                    HAS <bits>: 1 timer outputs, 2 edge capture, 4
 *                          Ticker and the timer API
 *   pad <pin> <n>          PAD <timer> <channel> <role> <route> <pwm>: the pad's
 *                          n-th way to a timer (CH32RVTimer.h; role 0 CHx, 1
 *                          CHxN, 2 BKIN; pwm 1 where analogWrite() may take the
 *                          timer, not tone()'s / Servo's TIM4 / TIM5), 0 0 0 0 0
 *                          past the last
 *   route <timer>          ROUTE <value>: the timer's AFIO remap now
 *   pwmseqcan <pin>        PWMSEQCAN <1 / 0>: CH32RVPwmSequence starts there (a timer with an update DMA request)
 *   pwmseq <pin> <hex>     PWMSEQ <ok> <top>: the bytes as WS2812 bits by CH32RVPwmSequence (800 kHz periods, T0H
 *                          32 % / T1H 64 % of one, then 60 periods low), waited for; the pad left a low output
 *   now                    NOW <micros()>: the DUT's timebase, for a test to
 *                          measure against the host's clock
 *   tpulsein <pin> <state> <timeout us>  TPULSEIN <us>: timerPulseIn()
 *                          (CH32RVTimerInput, the timer's capture)
 *   comp <pin> <pinN> <hz> <duty of 256> <dead ns>  COMP <ok> <dead ns set>
 *   compa <pin> <pinN> <hz> <duty of 256> <rise ns> <fall ns>
 *                                    setDeadTime(rise, fall): COMPA <begun> <taken> <the longer set> <the shorter>
 *   brk <pin> <level>      BRK <ok>: the bridge's break input, active at level
 *   brkstate <brk pin>     BRKSTATE <latched> <the pad's level>
 *   resume                 RESUME <ok>
 *   compoff                COMPOFF
 *   pwmt <a> <b> <hz> <align> <duty a> <duty b>  PWMT <ok>: CH32RVPwmTimer on
 *                          two pads of one timer, 10 bits, align 0 edge 1 center
 *   pwmtoff                PWMTOFF (also pwmsync's and pwmtrig's)
 *   itr <slave> <master>   ITR <n>: ch32rv_timer_trigger_from(), -1 for none
 *   pwmsync <a> <b> <hz>   PWMSYNC <ok> <a's timer> <b's timer>: 50 % on each,
 *                          b's timer started with a's (startWith)
 *   pwmtrig <out> <trig> <hz>  PWMTRIG <ok>: 50 % on out, waiting for a rising
 *                          edge on trig (startOn)
 *   phs <a> <b> <hz> <deg> PHS <ok>: CH32RVPhaseShift, a at 0, b at deg
 *   phsoff                 PHSOFF
 *   six <uh> <ul> <vh> <vl> <wh> <wl> <hz> <duty of 256>  SIX <ok>: CH32RVSixStep,
 *                          all off until sixstep
 *   sixstep <n>            SIXSTEP <ok>: step n (6 or more: all off)
 *   sixoff                 SIXOFF
 *   shot <pin> <delay us> <width us>  SHOT <ok>: one pulse, started now
 *   shotoff                SHOTOFF
 *   oc <pin> <period us> <at us> <mode>  OC <ok>: output compare (mode as
 *                          CH32RVOutputCompare::Mode)
 *   ocoff                  OCOFF
 *   icap <pin> <edge>      ICAP <ok>: CH32RVInputCapture at 1 MHz, edge 1
 *                          RISING, 2 FALLING, 3 CHANGE
 *   icread                 EDGES <n> <lost> <tick Hz> <first rising> <ticks
 *                          from each edge to the next>...
 *   icoff                  ICOFF
 *   pcount <pin> <edge> <ms>
 *                          PCOUNT <ok> <count> <us>: CH32RVPulseCounter (edge 1
 *                          RISING, 2 FALLING, 3 CHANGE) counted over ms, the
 *                          time it took by micros()
 *   spwm <a> <da> <b> <db> SPWM <ok>: SoftPWM, duty da / 255 on a, db on b
 *   spwmoff                SPWMOFF
 *   tick <pin> <us> <once> TICK <active>: a Ticker toggling pin every us, or
 *                          (once 1) pin high now and low after us
 *   tickoff                TICKOFF
 *   htimer <pin> <us> <once>  HTIMER <ok> <Hz>: timerBegin(1 MHz) and
 *                          timerAlarm(us) the same way
 *   htimeroff              HTIMEROFF */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVTimerInput.h>
#include <CH32RVTimerOutput.h>
#include "bench_console.h"
#include "internal/ch32rv_irq.h"
#include <Ticker.h>
#include <SoftPWM.h>
#include <Servo.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static char line[64];
static uint8_t used;
static Servo servo;
static CH32RVPulseCapture pulse;
static volatile uint32_t edges;

static void counted() { edges++; }

/* "<word> <a> <b> <c>\r\n" with as many numbers as given (plain print: the
 * formatted kind does not fit V003's 16 KB beside the rest). */
static void say(const char *word, int n = 0, long a = 0, long b = 0, long c = 0, long d = 0)
{
    CONSOLE.print(word);
    const long v[4] = {a, b, c, d};
    for (int k = 0; k < n; k++) {
        CONSOLE.print(' ');
        CONSOLE.print(v[k]);
    }
    CONSOLE.print("\r\n");
}

/* ---- The timer features (2026-11-17): pads on every route, complementary
 * PWM, one pulse, output compare, edge capture, pulseIn(), Ticker and the
 * timer API. What V003's 16 KB cannot hold beside the rest is left out of
 * its build; `has` says what is in. ---- */
#if !defined(CH32RV_VARIANT_CH32V003)
#define SIGNALS_TIMER_OUTPUT 1
#define SIGNALS_EDGE_CAPTURE 1
#define SIGNALS_TICKER 1
#endif
#define HAS_TIMER_OUTPUT 1
#define HAS_EDGE_CAPTURE 2
#define HAS_TICKER 4

#ifdef SIGNALS_TIMER_OUTPUT
static CH32RVComplementaryPwm bridge;
static CH32RVOnePulse shot;
static CH32RVOutputCompare compare;
static CH32RVPwmTimer pwmTimer, pwmTimer2;
static CH32RVPhaseShift phaseShift;
static CH32RVSixStep sixStep;
static CH32RVPwmSequence pwmSeq;
static uint16_t seqDuties[8 * 16 + 60];
#endif
#ifdef SIGNALS_EDGE_CAPTURE
static CH32RVInputCapture edgeCapture;
static CH32RVPulseCounter pulseCounter;
#endif
#ifdef SIGNALS_TICKER
static Ticker ticker;
static hw_timer_t *userTimer;
static volatile uint8_t tickPin;
static void tickToggle() { digitalWrite(tickPin, digitalRead(tickPin) ? LOW : HIGH); }
static void tickLow() { digitalWrite(tickPin, LOW); }
#endif

static bool timer_reply(int argc, char **argv, pin_size_t pin)
{
    const long a2 = argc > 2 ? atol(argv[2]) : 0, a3 = argc > 3 ? atol(argv[3]) : 0;
    const long a4 = argc > 4 ? atol(argv[4]) : 0, a5 = argc > 5 ? atol(argv[5]) : 0;
    (void)a4;
    (void)a5;
    if (!strcmp(argv[0], "has")) {
        long has = 0;
#ifdef SIGNALS_TIMER_OUTPUT
        has |= HAS_TIMER_OUTPUT;
#endif
#ifdef SIGNALS_EDGE_CAPTURE
        has |= HAS_EDGE_CAPTURE;
#endif
#ifdef SIGNALS_TICKER
        has |= HAS_TICKER;
#endif
        say("HAS", 1, has);
    } else if (!strcmp(argv[0], "pad") && argc == 3) {
        /* The pad's n-th way to a timer: timer, channel, role, route. */
        CH32RVTimerPad p = {};
        const bool found = digitalPinIsValid(pin) && ch32rv_timer_pad_find(pin, 7, (uint8_t)a2, &p);
        CONSOLE.print("PAD ");
        CONSOLE.print(found ? p.timer : 0);
        CONSOLE.print(' ');
        CONSOLE.print(p.channel);
        CONSOLE.print(' ');
        CONSOLE.print(p.role);
        CONSOLE.print(' ');
        CONSOLE.print(p.route);
        CONSOLE.print(' ');
        CONSOLE.print(found && ((CH32RV_PWM_TIMER_MASK >> p.timer) & 1u) ? 1 : 0);
        CONSOLE.print("\r\n");
    } else if (!strcmp(argv[0], "route") && argc == 2) {
        say("ROUTE", 1, ch32rv_timer_route((uint8_t)pin));
    } else if (!strcmp(argv[0], "now")) {
        CONSOLE.print("NOW ");
        CONSOLE.print((unsigned long)micros());
        CONSOLE.print("\r\n");
    } else if (!strcmp(argv[0], "tpulsein") && argc == 4) {
        pinMode(pin, INPUT);
        say("TPULSEIN", 1, (long)timerPulseIn(pin, (uint8_t)a2, (unsigned long)a3));
#ifdef SIGNALS_TIMER_OUTPUT
    } else if (!strcmp(argv[0], "comp") && argc == 6) {
        /* comp <pin> <pinN> <hz> <duty of 256> <dead ns> */
        bridge.setDeadTime((uint32_t)a5);
        const bool ok = bridge.begin(pin, (int)a2, (uint32_t)a3, 8);
        if (ok) {
            bridge.write((uint32_t)a4);
        }
        say("COMP", 2, ok, (long)bridge.deadTime());
    } else if (!strcmp(argv[0], "compa") && argc == 7) {
        /* compa <pin> <pinN> <hz> <duty of 256> <rise ns> <fall ns>: asymmetric dead time */
        const bool taken = bridge.setDeadTime((uint32_t)a5, (uint32_t)atol(argv[6]));
        const bool ok = bridge.begin(pin, (int)a2, (uint32_t)a3, 8);
        if (ok) {
            bridge.write((uint32_t)a4);
        }
        say("COMPA", 4, ok, taken, (long)bridge.deadTime(), (long)bridge.deadTimeShort());
    } else if (!strcmp(argv[0], "brk") && argc == 3) {
        say("BRK", 1, bridge.attachBreak(pin, (int)a2, a2 ? INPUT_PULLDOWN : INPUT_PULLUP));
    } else if (!strcmp(argv[0], "brkstate") && argc == 2) {
        /* the latch, and the level the break pad reads */
        say("BRKSTATE", 2, bridge.breakActive(), digitalRead(pin));
    } else if (!strcmp(argv[0], "resume")) {
        say("RESUME", 1, bridge.resume());
    } else if (!strcmp(argv[0], "compoff")) {
        bridge.end();
        CONSOLE.println("COMPOFF");
    } else if (!strcmp(argv[0], "pwmt") && argc == 7) {
        /* pwmt <a> <b> <hz> <align 0 edge / 1 center> <duty a of 1024> <duty b of 1024> */
        const bool ok = pwmTimer.begin(pin, (uint32_t)a3, 10, a4 ? CH32RVPwmTimer::CENTER : CH32RVPwmTimer::EDGE) &&
                        pwmTimer.attach((int)a2) && pwmTimer.write(pin, (uint32_t)a5) &&
                        pwmTimer.write((int)a2, (uint32_t)atol(argv[6]));
        say("PWMT", 1, ok);
    } else if (!strcmp(argv[0], "pwmtoff")) {
        pwmTimer2.end();
        pwmTimer.end();
        CONSOLE.println("PWMTOFF");
    } else if (!strcmp(argv[0], "itr") && argc == 3) {
        /* itr <slave timer> <master timer> */
        say("ITR", 1, ch32rv_timer_trigger_from((uint8_t)pin, (uint8_t)a2));
    } else if (!strcmp(argv[0], "pwmsync") && argc == 4) {
        /* pwmsync <a> <b> <hz>: 50 % on each, b's timer started with a's. startWith() before the duties: a's
         * counter restarts there, and a PWM already high would be cut short wherever it was (X035, 2026-11-21) */
        const bool ok = pwmTimer.begin(pin, (uint32_t)a3, 10) && pwmTimer2.begin((int)a2, (uint32_t)a3, 10) &&
                        pwmTimer2.startWith(pwmTimer) && pwmTimer.write(pin, 512) && pwmTimer2.write((int)a2, 512);
        say("PWMSYNC", 3, ok, pwmTimer.timer(), pwmTimer2.timer());
    } else if (!strcmp(argv[0], "pwmtrig") && argc == 4) {
        /* pwmtrig <out> <trigger> <hz>: 50 % on out, started by a rising edge on trigger */
        const bool ok = pwmTimer.begin(pin, (uint32_t)a3, 10) && pwmTimer.write(pin, 512) &&
                        pwmTimer.startOn((int)a2, RISING);
        say("PWMTRIG", 1, ok);
    } else if (!strcmp(argv[0], "phs") && argc == 5) {
        /* phs <a> <b> <hz> <b's degrees> */
        const bool ok = phaseShift.begin(pin, (uint32_t)a3) && phaseShift.attach((int)a2, (uint16_t)a4);
        say("PHS", 1, ok);
    } else if (!strcmp(argv[0], "phsoff")) {
        phaseShift.end();
        CONSOLE.println("PHSOFF");
    } else if (!strcmp(argv[0], "six") && argc == 9) {
        /* six <uh> <ul> <vh> <vl> <wh> <wl> <hz> <duty of 256>: all off until sixstep */
        const bool ok = sixStep.begin(pin, (int)a2, (int)a3, (int)a4, (int)a5, atoi(argv[6]), (uint32_t)atol(argv[7])) &&
                        sixStep.write((uint32_t)atol(argv[8]));
        say("SIX", 1, ok);
    } else if (!strcmp(argv[0], "sixstep") && argc == 2) {
        say("SIXSTEP", 1, sixStep.step((uint8_t)atoi(argv[1])));
    } else if (!strcmp(argv[0], "sixoff")) {
        sixStep.end();
        CONSOLE.println("SIXOFF");
    } else if (!strcmp(argv[0], "shot") && argc == 4) {
        /* shot <pin> <delay us> <width us>: one pulse, started now */
        const bool ok = (shot.isAttached() || shot.begin(pin)) && shot.fire((uint32_t)a2, (uint32_t)a3);
        say("SHOT", 1, ok);
    } else if (!strcmp(argv[0], "shotoff")) {
        shot.end();
        CONSOLE.println("SHOTOFF");
    } else if (!strcmp(argv[0], "pwmseqcan") && argc == 2) {
        const bool ok = pwmSeq.begin(pin, 800000);
        pwmSeq.end();
        pinMode(pin, INPUT);
        say("PWMSEQCAN", 1, ok);
    } else if (!strcmp(argv[0], "pwmseq") && argc == 3) {
        bool ok = pwmSeq.begin(pin, 800000);
        size_t n = 0;
        if (ok) {
            const uint16_t t0 = (uint16_t)(pwmSeq.top() * 32u / 100u), t1 = (uint16_t)(pwmSeq.top() * 64u / 100u);
            for (const char *h = argv[2]; h[0] && h[1] && n + 8 <= 8 * 16; h += 2) {
                char b[3] = {h[0], h[1], 0};
                const uint8_t v = (uint8_t)strtoul(b, nullptr, 16);
                for (int bit = 7; bit >= 0; bit--) {
                    seqDuties[n++] = (v >> bit) & 1 ? t1 : t0;
                }
            }
            for (int i = 0; i < 60; i++) {
                seqDuties[n++] = 0;
            }
            ok = pwmSeq.play(seqDuties, n);
            const uint32_t t = millis();
            while (ok && pwmSeq.busy() && millis() - t < 100) {
            }
            delayMicroseconds(100);
        }
        say("PWMSEQ", 2, ok, (long)pwmSeq.top());
        pwmSeq.end();
    } else if (!strcmp(argv[0], "oc") && argc == 5) {
        /* oc <pin> <period us> <at us> <mode> */
        const bool ok = (compare.isAttached() || compare.begin(pin, (uint32_t)a2)) &&
                        compare.set((CH32RVOutputCompare::Mode)a4, (uint32_t)a3);
        say("OC", 1, ok);
    } else if (!strcmp(argv[0], "ocoff")) {
        compare.end();
        CONSOLE.println("OCOFF");
#endif
#ifdef SIGNALS_EDGE_CAPTURE
    } else if (!strcmp(argv[0], "icap") && argc == 3) {
        /* icap <pin> <1 RISING, 2 FALLING, 3 CHANGE> */
        const int edge = a2 == 1 ? RISING : a2 == 2 ? FALLING : CHANGE;
        say("ICAP", 1, edgeCapture.begin(pin, edge, 1000000));
    } else if (!strcmp(argv[0], "icread")) {
        /* EDGES <n> <lost> <tick Hz> <first rising> then each edge after the
         * first as <ticks since the one before>, the queue's 8 at most */
        CH32RVInputCapture::Edge e[8];
        int n = 0;
        while (n < 8 && edgeCapture.read(e[n])) {
            n++;
        }
        CONSOLE.print("EDGES ");
        CONSOLE.print(n);
        CONSOLE.print(' ');
        CONSOLE.print(edgeCapture.lost());
        CONSOLE.print(' ');
        CONSOLE.print(edgeCapture.tickHz());
        CONSOLE.print(' ');
        CONSOLE.print(n ? (int)e[0].rising : -1);
        for (int k = 1; k < n; k++) {
            CONSOLE.print(' ');
            CONSOLE.print((unsigned long)(e[k].ticks - e[k - 1].ticks));
        }
        CONSOLE.print("\r\n");
    } else if (!strcmp(argv[0], "icoff")) {
        edgeCapture.end();
        CONSOLE.println("ICOFF");
    } else if (!strcmp(argv[0], "pcount") && argc == 4) {
        const int edge = a2 == 1 ? RISING : a2 == 2 ? FALLING : CHANGE;
        const bool ok = pulseCounter.begin(pin, edge);
        uint64_t n = 0;
        uint32_t took = 0;
        if (ok) {
            const uint32_t irq = ch32rv_irq_save();
            pulseCounter.clear();
            const uint32_t t0 = micros();
            ch32rv_irq_restore(irq);
            delay((uint32_t)a3);
            const uint32_t irq2 = ch32rv_irq_save();
            n = pulseCounter.count();
            took = micros() - t0;
            ch32rv_irq_restore(irq2);
        }
        pulseCounter.end();
        CONSOLE.print("PCOUNT ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)n);
        CONSOLE.print(' ');
        CONSOLE.println((unsigned long)took);
#endif
#ifdef SIGNALS_TICKER
    } else if (!strcmp(argv[0], "tick") && argc == 4) {
        /* tick <pin> <us> <0 periodic toggle / 1 once: high now, low after us> */
        tickPin = (uint8_t)pin;
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
        /* The line goes high right after the Ticker is armed: taking the
         * timer (timerBegin) comes before, outside the pulse. */
        if (a3) {
            ticker.once_us((uint64_t)a2, tickLow);
            digitalWrite(pin, HIGH);
        } else {
            ticker.attach_us((uint64_t)a2, tickToggle);
        }
        say("TICK", 1, ticker.active());
    } else if (!strcmp(argv[0], "spwm") && argc == 5) {
        /* spwm <a> <duty a of 255> <b> <duty b of 255> */
        const bool ok = SoftPWMBegin();
        SoftPWMSet((int)pin, (uint8_t)a2, 1);
        SoftPWMSet((int)a3, (uint8_t)a4, 1);
        say("SPWM", 1, ok);
    } else if (!strcmp(argv[0], "spwmoff")) {
        SoftPWMEnd(ALL);
        CONSOLE.println("SPWMOFF");
    } else if (!strcmp(argv[0], "tickoff")) {
        ticker.detach();
        CONSOLE.println("TICKOFF");
    } else if (!strcmp(argv[0], "htimer") && argc == 4) {
        /* htimer <pin> <us> <0 alarm every us, toggling / 1 one alarm: high now, low at us> */
        tickPin = (uint8_t)pin;
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
        if (!userTimer) {
            userTimer = timerBegin(1000000);
        }
        if (userTimer) {
            timerAttachInterrupt(userTimer, a3 ? tickLow : tickToggle);
            timerWrite(userTimer, 0);
            timerAlarm(userTimer, (uint64_t)a2, !a3, 0);
            if (a3) {
                digitalWrite(pin, HIGH);     /* right after the alarm is armed */
            }
        }
        say("HTIMER", 2, userTimer != nullptr, userTimer ? (long)timerGetFrequency(userTimer) : 0);
    } else if (!strcmp(argv[0], "htimeroff")) {
        timerEnd(userTimer);
        userTimer = nullptr;
        CONSOLE.println("HTIMEROFF");
#endif
    } else {
        return false;
    }
    return true;
}

static void reply(char *command)
{
    char *argv[10];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 10; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    const pin_size_t pin = argc > 1 ? (pin_size_t)atoi(argv[1]) : 0;
    if (!strcmp(argv[0], "ready") && argc == 2) {
        CONSOLE.print("READY ");
        CONSOLE.print(argv[1]);
        CONSOLE.print(' ');
        CONSOLE.print(BENCH_BUILD_ID);
        CONSOLE.print("\r\n");
    } else if (!strcmp(argv[0], "restart")) {
        CH32RV.restart();
    } else if (!strcmp(argv[0], "caps") && argc == 2) {
        const bool valid = digitalPinIsValid(pin);
        say("CAPS", 3, pin, valid ? (long)CH32RV_PWM_PIN_TO_TIMER(pin) : 0, valid ? (long)CH32RV_PWM_PIN_TO_CHANNEL(pin) : 0);
    } else if (!strcmp(argv[0], "servo") && argc == 3) {
        const bool ok = servo.attached() || servo.attach(pin) != INVALID_SERVO;
        if (ok) {
            servo.writeMicroseconds(atoi(argv[2]));
        }
        say("SERVO", 1, ok);
    } else if (!strcmp(argv[0], "servooff")) {
        servo.detach();
        CONSOLE.println("SERVOOFF");
    } else if (!strcmp(argv[0], "tone") && argc == 3) {
        tone(pin, (unsigned int)atol(argv[2]));
        CONSOLE.println("TONE");
    } else if (!strcmp(argv[0], "toned") && argc == 4) {
        tone(pin, (unsigned int)atol(argv[2]), (unsigned long)atol(argv[3]));
        CONSOLE.println("TONE");
    } else if (!strcmp(argv[0], "notone") && argc == 2) {
        noTone(pin);
        CONSOLE.println("NOTONE");
    } else if (!strcmp(argv[0], "level") && argc == 3) {
        digitalWrite(pin, atoi(argv[2]) ? HIGH : LOW);
        CONSOLE.println("LEVEL");
    } else if (!strcmp(argv[0], "pulsein") && argc == 5) {
        pinMode(pin, INPUT);
        const uint8_t state = (uint8_t)atoi(argv[2]);
        const unsigned long timeout = (unsigned long)atol(argv[3]);
        const unsigned long us = atoi(argv[4]) ? pulseInLong(pin, state, timeout) : pulseIn(pin, state, timeout);
        CONSOLE.print("PULSEIN ");
        CONSOLE.println(us);
    } else if (!strcmp(argv[0], "input") && argc == 2) {
        pinMode(pin, INPUT);
        CONSOLE.println("INPUT");
    } else if (!strcmp(argv[0], "pwm") && argc == 4) {
        /* Any pad a timer channel or complementary output reaches, on any route. */
        CH32RVTimerPad any;
        bool ok = false;
        for (uint8_t n = 0; digitalPinIsValid(pin) && !ok &&
                            ch32rv_timer_pad_find(pin, CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CH) |
                                                       CH32RV_TIMER_ROLE(CH32RV_TIMER_PAD_CHN), n, &any);
             n++) {
            ok = ((CH32RV_PWM_TIMER_MASK >> any.timer) & 1u) != 0u;
        }
        if (ok) {
            analogWriteFrequency(pin, (uint32_t)atol(argv[3]));
            analogWrite(pin, atoi(argv[2]));
        }
        say("PWM", 1, ok);
    } else if (!strcmp(argv[0], "pwmoff") && argc == 2) {
        analogWrite(pin, 0);
        CONSOLE.println("PWMOFF");
    } else if (!strcmp(argv[0], "irq") && argc == 3) {
        const int mode = atoi(argv[2]);
        const PinStatus how = mode == 1 ? RISING : mode == 2 ? FALLING : CHANGE;
        const int irq = digitalPinToInterrupt(pin);
        pinMode(pin, INPUT);
        edges = 0;
        if (irq != NOT_AN_INTERRUPT) {
            attachInterrupt(irq, counted, how);
        }
        say("IRQ", 1, irq != NOT_AN_INTERRUPT);
    } else if (!strcmp(argv[0], "count")) {
        say("COUNT", 1, (long)edges);
    } else if (!strcmp(argv[0], "irqoff") && argc == 2) {
        detachInterrupt(digitalPinToInterrupt(pin));
        CONSOLE.println("IRQOFF");
    } else if (!strcmp(argv[0], "cap") && argc == 2) {
        say("CAP", 1, pulse.begin(pin, 10));
    } else if (!strcmp(argv[0], "capread")) {
        CH32RVPulseCapture::Pulse p;
        if (pulse.read(p)) {
            say("PULSE", 3, (long)p.period, (long)p.high, (long)p.tickHz);
        } else {
            CONSOLE.println("PULSE NONE");
        }
    } else if (!strcmp(argv[0], "capoff")) {
        pulse.end();
        CONSOLE.println("CAPOFF");
    } else if (!strcmp(argv[0], "rgb") && argc == 3) {
        const int n = atoi(argv[2]);
        for (int i = 0; i < n; i++) {
            rgbLedWrite(pin, (uint8_t)(0x12 + i), 0xA5, (uint8_t)(0x3C ^ i));
        }
        delayMicroseconds(300);
        say("RGB", 1, n);
    } else if (!strcmp(argv[0], "rgbbuf") && argc == 3) {
        uint8_t grb[24];
        const int n = atoi(argv[2]) < 8 ? atoi(argv[2]) : 8;
        for (int i = 0; i < n; i++) {
            grb[3 * i] = 0xA5;
            grb[3 * i + 1] = (uint8_t)(0x12 + i);
            grb[3 * i + 2] = (uint8_t)(0x3C ^ i);
        }
        ch32rv_rgb_led_write(pin, grb, (size_t)(3 * n));
        delayMicroseconds(300);
        say("RGB", 1, n);
    } else if (!strcmp(argv[0], "rgbhw") && argc == 2) {
        say("RGBHW", 1, ch32rv_rgb_led_is_hardware(pin) ? 1 : 0);
    } else if (timer_reply(argc, argv, pin)) {
    } else {
        CONSOLE.print("UNKNOWN ");
        CONSOLE.print(argv[0]);
        CONSOLE.print("\r\n");
    }
}

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
}

void loop()
{
    while (CONSOLE.available()) {
        const int c = CONSOLE.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
