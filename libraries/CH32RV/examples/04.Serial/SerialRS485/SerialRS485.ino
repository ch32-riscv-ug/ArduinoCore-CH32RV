// en: Talk on an RS485 bus through a transceiver whose driver enable the core switches around each write.
// ja: 送信のたびにコアが方向を切り替えるRS485のトランシーバーで、バスに問い合わせる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: SERIAL1, A1 */

// en: The transceiver's DE (and /RE, tied to it): any pin, high while this board sends.
// ja: トランシーバーのDE（/REもつなぐ）。どの端子でもよい。送る間だけHigh。
static const int DE_PIN = A1;
static const uint32_t REPLY_TIMEOUT_MS = 200;

void setup()
{
    Serial.begin(115200);
    // en: The RTS pin of setPins() is the driver enable in RS485 mode (as on an ESP32).
    // ja: RS485のモードでは、setPins()のRTSの端子がDEになる（ESP32と同じ）。
    const bool ok = Serial1.setPins(-1, -1, -1, DE_PIN) && Serial1.setMode(UART_MODE_RS485_HALF_DUPLEX);
    Serial1.begin(9600);
    Serial.println(ok ? "RS485 ready" : "RS485 mode refused");
}

void loop()
{
    static uint32_t lastQuery;
    if ((uint32_t)(millis() - lastQuery) < 1000) {
        return;
    }
    lastQuery = millis();
    // en: DE rises before the first start bit and falls after the last stop bit, by itself.
    // ja: DEは最初のスタートビットの前に上がり、最後のストップビットの後に下がる。
    Serial1.print("?\n");
    const uint32_t start = millis();
    bool any = false;
    while ((uint32_t)(millis() - start) < REPLY_TIMEOUT_MS) {
        if (Serial1.available()) {
            Serial.write(Serial1.read());
            any = true;
        }
    }
    if (!any) {
        Serial.println("no reply");
    }
}
