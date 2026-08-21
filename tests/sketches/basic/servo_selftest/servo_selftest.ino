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

static const uint8_t PIN = LED_BUILTIN;

static int failures;
static Servo servo;

static void check(const char *name, bool ok)
{
    Serial.print(name);
    Serial.println(ok ? " PASS" : " FAIL");
    if (!ok) {
        failures++;
    }
}

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

void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    delay(50);
    Serial.println("servo_selftest start");

#ifdef CH32_SERVO_TIMER
    check("attach_succeeds", servo.attach(PIN) != INVALID_SERVO);
    check("reports_attached", servo.attached());

    /* Default is 1500 us. Anything between 1.2 and 1.8 ms means the frame is
     * running and the width is the one the library intends. */
    uint32_t us = measure_pulse(100);
    check("default_pulse_width", us > 1200 && us < 1800);

    servo.writeMicroseconds(1000);
    us = measure_pulse(100);
    check("write_microseconds", us > 700 && us < 1300);

    servo.write(180);
    us = measure_pulse(100);
    check("write_angle_high", us > 2000 && us < 2700);

    servo.write(0);
    us = measure_pulse(100);
    check("write_angle_low", us > 300 && us < 900);

    check("read_back_angle", servo.read() < 20);

    /* The frame repeats: a second pulse has to arrive within one interval. */
    check("frame_repeats", measure_pulse(50) > 300);

    servo.detach();
    check("detach_reported", !servo.attached());
    delay(30);
    check("detach_leaves_low", digitalRead(PIN) == LOW);
#else
    Serial.println("attach_succeeds SKIP no timer to spare on this series");
    Serial.println("reports_attached SKIP no timer to spare on this series");
    Serial.println("default_pulse_width SKIP no timer to spare on this series");
    Serial.println("write_microseconds SKIP no timer to spare on this series");
    Serial.println("write_angle_high SKIP no timer to spare on this series");
    Serial.println("write_angle_low SKIP no timer to spare on this series");
    Serial.println("read_back_angle SKIP no timer to spare on this series");
    Serial.println("frame_repeats SKIP no timer to spare on this series");
    Serial.println("detach_reported SKIP no timer to spare on this series");
    Serial.println("detach_leaves_low SKIP no timer to spare on this series");
#endif

    Serial.print("servo_selftest done failures=");
    Serial.println(failures);
}

void loop()
{
}
