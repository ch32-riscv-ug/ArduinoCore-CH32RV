// en: Start the watchdog on command and demonstrate recovery from stopped feeding.
// ja: 操作でwatchdogを開始し、給餌停止からの再起動を確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

static bool enabled;
static bool feed = true;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.print("Reset reason: ");
    Serial.println(CH32RV.resetReasonName());
    // en: Every boot starts unarmed, so the demonstration cannot create a reset loop.
    // ja: 起動時には毎回開始前へ戻し、リセットを繰り返すデモにしない。
    Serial.println("e: enable watchdog, x: stop feeding after enable");
}

void loop()
{
    if (Serial.available()) {
        const int command = Serial.read();
        if (command == 'e' && !enabled) {
            // en: The requested interval is approximate; keep normal work much shorter.
            // ja: 指定時間は概算。通常の処理は十分短く終える。
            enabled = CH32RV.wdtEnable(2000);
            Serial.println(enabled ? "Watchdog enabled" : "Watchdog unavailable");
        } else if (command == 'x' && enabled && feed) {
            feed = false;
            Serial.println("Feeding stopped; waiting for reset");
        }
    }
    // en: Stop feeding without blocking loop(); there is no watchdog-disable API.
    // ja: loop()を止めずに給餌だけを止める。watchdogを解除するAPIはない。
    if (enabled && feed) {
        CH32RV.wdtFeed();
    }
}
