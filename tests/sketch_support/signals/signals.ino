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
 *   level <pin> <0/1>      LEVEL: digitalWrite(pin, v)
 *   irq <pin> <mode>       IRQ <ok>: attachInterrupt, mode 1 RISING, 2 FALLING,
 *                          3 CHANGE; the count starts at 0
 *   count                  COUNT <edges seen>
 *   irqoff <pin>           IRQOFF
 *   cap <pin>              CAP <ok>: CH32RVPulseCapture on pin (10 Hz up)
 *   capread                PULSE <period ticks> <high ticks> <tick Hz>, or
 *                          PULSE NONE
 *   capoff                 CAPOFF */
#include <Arduino.h>
#include <CH32RV.h>
#include <CH32RVTimerInput.h>
#include <SerialDMSeq.h>
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
static void say(const char *word, int n = 0, long a = 0, long b = 0, long c = 0)
{
    SerialDMSeq.print(word);
    const long v[3] = {a, b, c};
    for (int k = 0; k < n; k++) {
        SerialDMSeq.print(' ');
        SerialDMSeq.print(v[k]);
    }
    SerialDMSeq.print("\r\n");
}

static void reply(char *command)
{
    char *argv[4];
    int argc = 0;
    for (char *t = strtok(command, " "); t && argc < 4; t = strtok(nullptr, " ")) {
        argv[argc++] = t;
    }
    if (!argc) {
        return;
    }
    const pin_size_t pin = argc > 1 ? (pin_size_t)atoi(argv[1]) : 0;
    if (!strcmp(argv[0], "ready") && argc == 2) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(argv[1]);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print("\r\n");
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
        SerialDMSeq.println("SERVOOFF");
    } else if (!strcmp(argv[0], "tone") && argc == 3) {
        tone(pin, (unsigned int)atol(argv[2]));
        SerialDMSeq.println("TONE");
    } else if (!strcmp(argv[0], "toned") && argc == 4) {
        tone(pin, (unsigned int)atol(argv[2]), (unsigned long)atol(argv[3]));
        SerialDMSeq.println("TONE");
    } else if (!strcmp(argv[0], "notone") && argc == 2) {
        noTone(pin);
        SerialDMSeq.println("NOTONE");
    } else if (!strcmp(argv[0], "level") && argc == 3) {
        digitalWrite(pin, atoi(argv[2]) ? HIGH : LOW);
        SerialDMSeq.println("LEVEL");
    } else if (!strcmp(argv[0], "input") && argc == 2) {
        pinMode(pin, INPUT);
        SerialDMSeq.println("INPUT");
    } else if (!strcmp(argv[0], "pwm") && argc == 4) {
        const bool ok = digitalPinIsValid(pin) && CH32RV_PWM_PIN_TO_TIMER(pin) && CH32RV_PWM_PIN_TO_CHANNEL(pin);
        if (ok) {
            analogWriteFrequency(pin, (uint32_t)atol(argv[3]));
            analogWrite(pin, atoi(argv[2]));
        }
        say("PWM", 1, ok);
    } else if (!strcmp(argv[0], "pwmoff") && argc == 2) {
        analogWrite(pin, 0);
        SerialDMSeq.println("PWMOFF");
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
        SerialDMSeq.println("IRQOFF");
    } else if (!strcmp(argv[0], "cap") && argc == 2) {
        say("CAP", 1, pulse.begin(pin, 10));
    } else if (!strcmp(argv[0], "capread")) {
        CH32RVPulseCapture::Pulse p;
        if (pulse.read(p)) {
            say("PULSE", 3, (long)p.period, (long)p.high, (long)p.tickHz);
        } else {
            SerialDMSeq.println("PULSE NONE");
        }
    } else if (!strcmp(argv[0], "capoff")) {
        pulse.end();
        SerialDMSeq.println("CAPOFF");
    } else {
        SerialDMSeq.print("UNKNOWN ");
        SerialDMSeq.print(argv[0]);
        SerialDMSeq.print("\r\n");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
}

void loop()
{
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
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
