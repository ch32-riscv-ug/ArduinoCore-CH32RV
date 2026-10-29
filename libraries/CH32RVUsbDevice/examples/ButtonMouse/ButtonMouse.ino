// en: Hold and release the USB mouse left button with a debounced switch.
// ja: チャタリングを除いたスイッチでUSBマウスの左ボタンを押下・解放する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USBFS */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidMouse mouse(device);
static const uint8_t BUTTON_PIN = PA1;
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
static bool reportPending = true;
static bool sentPressed;
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
        reportPending = true;
    }
}
void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    candidateLevel = stableLevel = digitalRead(BUTTON_PIN);
    changedAtMs = millis();
    device.onBusEvent(onBusEvent);
    CH32RVUsbDeviceConfig config;
    config.product = "CH32RV Button Mouse";
    config.serialNumber = "ch32rv-button-mouse";
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
    if (!usbStarted || !device.ready()) {
        armed = false;
        reportPending = true;
        return;
    }
    // en: A stable release arms input; plugging in while held does not press a host key/button.
    // ja: 安定した解放状態で入力を有効にする。押したまま接続してもホストを押下状態にしない。
    if (stableLevel == HIGH && candidateLevel == HIGH &&
        (uint32_t)(now - changedAtMs) >= DEBOUNCE_MS) {
        armed = true;
    }
    const bool pressed = armed && stableLevel == LOW;
    if ((!reportPending && pressed == sentPressed) ||
        (uint32_t)(now - lastReportMs) < 10) {
        return;
    }
    lastReportMs = now;

    // en: Send the complete held-button state with zero relative motion; release uses buttons = 0.
    // ja: ボタンの全状態を送り、相対移動は0にする。解放時はbuttonsを0にする。
    CH32RVUsbDeviceBootMouseReport report;
    report.buttons = pressed ? CH32RV_USB_DEVICE_MOUSE_LEFT : 0;
    const bool ok = mouse.sendReport(report, 0);
    if (ok) {
        sentPressed = pressed;
        reportPending = false;
    } else if ((uint32_t)(now - lastErrorMs) >= 1000) {
        lastErrorMs = now;
        Serial.println(device.lastErrorName());
    }
    // en: On failure, keep the previous accepted state and retry the latest physical state later.
    // ja: 失敗時は送信済み状態を更新せず、次回に最新の入力状態を送り直す。
}
