// en: Drive a servo by pulse width or angle from the serial console, and attach / detach it.
// ja: シリアルから、パルス幅または角度でサーボを動かす。接続と切り離しもする。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <Servo.h>

static const uint8_t SERVO_PIN = PA1;
/* en: The pulse range this servo accepts; many take 1000-2000 us, some 500-2500. Outside it they strain.
 * ja: このサーボが受け付けるパルスの範囲。多くは1000〜2000 us、500〜2500 usのものもある。外れると無理がかかる。 */
static const int MIN_US = 1000;
static const int MAX_US = 2000;

static Servo servo;
static char line[16];
static size_t used;

static void report()
{
    if (!servo.attached()) {
        Serial.println("detached: no pulses (the servo is free)");
        return;
    }
    Serial.print(servo.readMicroseconds());
    Serial.print(" us = ");
    Serial.print(servo.read());
    Serial.println(" deg");
}

static void attachServo()
{
    // en: min and max map 0 and 180 degrees, and clamp writeMicroseconds() too.
    // ja: minとmaxが0度と180度になり、writeMicroseconds()もこの範囲に収める。
    if (servo.attach(SERVO_PIN, MIN_US, MAX_US) == INVALID_SERVO) {
        Serial.println("no timer free for Servo on this series");
    }
}

static void command(const char *s)
{
    if (!s[0]) {
        return;                                // en: an empty line (CR then LF) / ja: 空行（CRのあとのLF）
    }
    if (s[0] == 'a') {
        servo.write(atoi(s + 1));              // en: degrees, 0..180 / ja: 角度0〜180
    } else if (s[0] == 'd') {
        servo.detach();
    } else if (s[0] == 'r') {
        attachServo();
    } else if (s[0] >= '0' && s[0] <= '9') {
        servo.writeMicroseconds(atoi(s));      // en: clamped to MIN_US..MAX_US / ja: MIN_US〜MAX_USに収まる
    } else {
        Serial.println("commands: 1500 (us), a90 (degrees), d (detach), r (attach)");
        return;
    }
    report();
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    attachServo();
    servo.writeMicroseconds((MIN_US + MAX_US) / 2);
    Serial.println("commands: 1500 (us), a90 (degrees), d (detach), r (attach)");
    report();
}

void loop()
{
    // en: Collect a line without waiting; the pulses keep going from the timer meanwhile.
    // ja: 待たずに1行を集める。その間もパルスはタイマーが出し続ける。
    while (Serial.available()) {
        const int c = Serial.read();
        if (c == '\n' || c == '\r') {
            line[used] = 0;
            command(line);
            used = 0;
        } else if (used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
