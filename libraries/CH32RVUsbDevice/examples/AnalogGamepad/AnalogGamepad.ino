// en: Read two centered analog axes and a debounced button as a USB gamepad.
// ja: 中央補正した2つのアナログ軸と、チャタリングを除いたボタンをUSBゲームパッドへ送る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USBFS, ADC1 */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidGamepad gamepad(device);
static const uint8_t X_PIN = A0;
static const uint8_t Y_PIN = A1;
static const uint8_t BUTTON_PIN = PB0;
static const int ADC_MAX = 4095;
static const int DEAD_ZONE = 64;
static int centerX, centerY;
static const uint32_t DEBOUNCE_MS = 25;
static int candidateLevel = HIGH;
static int stableLevel = HIGH;
static uint32_t changedAtMs;

static void updateButton(uint32_t now)
{
    const int level = digitalRead(BUTTON_PIN);
    if (level != candidateLevel) {
        candidateLevel = level;
        changedAtMs = now;
    }
    // en: Accept a new state only after the input stays unchanged for the debounce interval.
    // ja: 入力が一定時間変わらなかった場合だけ、新しい状態を確定する。
    if ((uint32_t)(now - changedAtMs) >= DEBOUNCE_MS) {
        stableLevel = candidateLevel;
    }
}
static bool usbStarted;
static bool armed;
static uint32_t lastReportMs;
static uint32_t lastErrorMs;

static void onBusEvent(CH32RVUsbBusEvent event)
{
    if (event == CH32RVUsbBusEvent::Reset ||
        event == CH32RVUsbBusEvent::Configured ||
        event == CH32RVUsbBusEvent::Unconfigured) {
        // en: Reconfiguration starts a fresh session; require a released button before arming.
        // ja: 再認識は新しい接続として扱い、ボタンの解放を確認してから入力を有効にする。
        armed = false;
    }
}

static int readCenter(uint8_t pin)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        sum += analogRead(pin);
        // en: Only setup calibrates with a short wait; normal input processing never waits here.
        // ja: 短い待機はsetup()での校正だけ。通常の入力処理ではここで待たない。
        delay(2);
    }
    return sum / 32;
}

static int8_t axisValue(int raw, int center)
{
    if (raw < 0) raw = 0;
    if (raw > ADC_MAX) raw = ADC_MAX;
    // en: Reserve a neutral band, then scale each side independently to reach both endpoints.
    // ja: 中央に中立帯を設け、左右を別々に換算して両端まで使えるようにする。
    if (raw > center + DEAD_ZONE) {
        return (int32_t)(raw - center - DEAD_ZONE) * 127 / (ADC_MAX - center - DEAD_ZONE);
    }
    if (raw < center - DEAD_ZONE) {
        return -(int32_t)(center - DEAD_ZONE - raw) * 127 / (center - DEAD_ZONE);
    }
    return 0;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    candidateLevel = stableLevel = digitalRead(BUTTON_PIN);
    changedAtMs = millis();
    analogReadResolution(12);
    Serial.println("Calibrating: leave both axes centered");
    centerX = readCenter(X_PIN);
    centerY = readCenter(Y_PIN);
    // en: Reject a center near either rail before the mapping could have a zero denominator.
    // ja: 換算式の分母が0にならないよう、電源端に近すぎる中央値は開始前に拒否する。
    if (centerX <= DEAD_ZONE || centerX >= ADC_MAX - DEAD_ZONE ||
        centerY <= DEAD_ZONE || centerY >= ADC_MAX - DEAD_ZONE) {
        Serial.println("Invalid center: check wiring, center the axes, then reset");
        return;
    }
    device.onBusEvent(onBusEvent);
    CH32RVUsbDeviceConfig config;
    config.product = "CH32RV Analog Gamepad";
    config.serialNumber = "ch32rv-analog-gamepad";
    usbStarted = device.begin(config);
    if (!usbStarted) {
        Serial.println(device.lastErrorName());
    }
}

void loop()
{
    deliverEvents();
    const uint32_t now = millis();
    updateButton(now);
    if (!usbStarted || !device.mounted()) {
        armed = false;
        return;
    }
    if (stableLevel == HIGH && candidateLevel == HIGH &&
        (uint32_t)(now - changedAtMs) >= DEBOUNCE_MS) {
        armed = true;
    }
    if ((uint32_t)(now - lastReportMs) < 10) {
        return;
    }
    lastReportMs = now;
    CH32RVUsbDeviceGamepadReport report;
    report.x = axisValue(analogRead(X_PIN), centerX);
    report.y = axisValue(analogRead(Y_PIN), centerY);
    // en: Other axes stay at zero and the default hat stays centered; button one is bitmap bit zero.
    // ja: ほかの軸は0、hatは既定の中立のまま。ボタン1はbitmapのbit 0を使う。
    report.buttons = armed && stableLevel == LOW ? 1UL : 0;
    // en: Send current state periodically; a busy endpoint is retried with fresh input next time.
    // ja: 現在の全状態を定期送信する。endpointが使用中なら、次回の新しい入力で再試行する。
    if (!gamepad.sendReport(report, 0) && (uint32_t)(now - lastErrorMs) >= 1000) {
        lastErrorMs = now;
        Serial.println(device.lastErrorName());
    }
}
