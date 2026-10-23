// en: Show the reset reason and restart only on an explicit UART command.
// ja: リセット理由を表示し、UARTの明示的な操作で再起動する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.print("Reset reason: ");
    Serial.println(CH32RV.resetReasonName());
    Serial.println("Send r to restart");
}

void loop()
{
    if (Serial.available() && Serial.read() == 'r') {
        Serial.println("Software restart");
        // en: Wait for the outgoing message before resetting; restart() never returns.
        // ja: 送信完了を待ってからリセットする。restart()からは戻らない。
        Serial.flush();
        CH32RV.restart();
    }
}
