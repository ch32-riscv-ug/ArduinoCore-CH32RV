/* Sweep - move a servo from end to end and back.
 *
 * Wiring: the servo's signal wire to the pin, and its power to a supply that
 * can take the stall current - **not** the board's 3V3 regulator. Share the
 * ground. A servo browning out the board mid-sweep is the usual first result
 * of skipping that.
 *
 * Which timer the series gave Servo is in the variant header as
 * CH32_SERVO_TIMER. It is never the one tone() uses, so a buzzer and a servo
 * can run at once; on the small parts it does share a timer with analogWrite(),
 * and the header names the pads that stop fading while a servo is attached.
 */
#include <Servo.h>

/* Change to your servo's pin. Servo drives any pad from a timer interrupt, so
 * PC0 is just a pad that exists on the series this example is built for. */
static const uint8_t SERVO_PIN = PC0;

static Servo servo;

void setup()
{
    Serial.begin(115200);
    if (servo.attach(SERVO_PIN) == INVALID_SERVO) {
        Serial.println("no timer free for Servo on this series");
    }
}

void loop()
{
    for (int angle = 0; angle <= 180; angle += 2) {
        servo.write(angle);
        delay(15);              /* the servo needs time to get there */
    }
    for (int angle = 180; angle >= 0; angle -= 2) {
        servo.write(angle);
        delay(15);
    }
}
