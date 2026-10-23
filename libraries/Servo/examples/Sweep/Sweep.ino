// en: Move a servo back and forth through a range of angles.
// ja: サーボの目標角度を変え、一定の範囲を往復させる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <Servo.h>

static const uint8_t SERVO_PIN = PA1;

static Servo servo;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: Check timer acquisition before treating the servo as attached.
    // ja: サーボの接続成功と見なす前に、タイマー取得の成否を確認する。
    if (servo.attach(SERVO_PIN) == INVALID_SERVO) {
        Serial.println("no timer free for Servo on this series");
    }
}

void loop()
{
    for (int angle = 0; angle <= 180; angle += 2) {
        // en: Command a target angle; position feedback is not read, so allow time before the next target.
        // ja: 目標角を指示する。到達を読み取る処理はないので、次の指示まで動く時間を取る。
        servo.write(angle);
        // en: Allow mechanical movement; write() does not wait for the target position.
        // ja: write()は到達を待たないため、機械的に動く時間を取る。
        delay(15);
    }
    for (int angle = 180; angle >= 0; angle -= 2) {
        servo.write(angle);
        delay(15);
    }
}
