/* Servo with nothing attached to the pin.
 *
 * The pad reads back its own level, so the sketch can time its own pulses: a
 * servo signal is a ~1.5 ms high pulse every 20 ms, and both numbers are
 * checkable without a servo, an oscilloscope or any wiring.
 *
 * Polling is not precise, so the bounds are wide. What they catch is the
 * failure that matters: a frame that never happens, a pulse stuck high, or a
 * width that ignores writeMicroseconds().
 */
#include <Servo.h>

#include "testcmd.h"

static const uint8_t PIN = PA1;

static Servo servo;

/* Width of the next high pulse in microseconds, or 0 if none started in time. */
static uint32_t measure_pulse(uint32_t timeout_ms)
{
    const uint32_t deadline = millis() + timeout_ms;
    while (digitalRead(PIN) == HIGH) {          /* wait out one in progress */
        if (millis() > deadline) {
            return 0;
        }
    }
    while (digitalRead(PIN) == LOW) {           /* wait for the next rise */
        if (millis() > deadline) {
            return 0;
        }
    }
    const uint32_t start = micros();
    while (digitalRead(PIN) == HIGH) {
        if (micros() - start > 50000ul) {
            return 0;                           /* stuck high */
        }
    }
    return micros() - start;
}

static void run_checks()
{
#ifdef CH32_SERVO_TIMER
    tc_check("attach_succeeds", servo.attach(PIN) != INVALID_SERVO);
    tc_check("reports_attached", servo.attached());

    /* Default is 1500 us. Anything between 1.2 and 1.8 ms means the frame is
     * running and the width is the one the library intends. */
    uint32_t us = measure_pulse(100);
    tc_check("default_pulse_width", us > 1200 && us < 1800);

    servo.writeMicroseconds(1000);
    us = measure_pulse(100);
    tc_check("write_microseconds", us > 700 && us < 1300);

    servo.write(180);
    us = measure_pulse(100);
    tc_check("write_angle_high", us > 2000 && us < 2700);

    servo.write(0);
    us = measure_pulse(100);
    tc_check("write_angle_low", us > 300 && us < 900);

    tc_check("read_back_angle", servo.read() < 20);

    /* The frame repeats: a second pulse has to arrive within one interval. */
    tc_check("frame_repeats", measure_pulse(50) > 300);

    servo.detach();
    tc_check("detach_reported", !servo.attached());
    delay(30);
    tc_check("detach_leaves_low", digitalRead(PIN) == LOW);
#else
    static const char *const WHY = "no timer to spare on this series";
    tc_skip("attach_succeeds", WHY);
    tc_skip("reports_attached", WHY);
    tc_skip("default_pulse_width", WHY);
    tc_skip("write_microseconds", WHY);
    tc_skip("write_angle_high", WHY);
    tc_skip("write_angle_low", WHY);
    tc_skip("read_back_angle", WHY);
    tc_skip("frame_repeats", WHY);
    tc_skip("detach_reported", WHY);
    tc_skip("detach_leaves_low", WHY);
#endif

    tc_done();
}

void setup()
{
    tc_begin("servo_selftest");
}

void loop()
{
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
