// en: Start the window watchdog on command, then feed it too late or too early and see the reset.
// ja: 操作で窓ウォッチドッグを開始し、餌が遅すぎる・早すぎるときのリセットを確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>

// en: The early-warning count kept across the reset that follows it (RAM the startup does not clear).
// ja: 早期警告の回数を、そのあとのリセットをまたいで残す（起動で消さないRAM）。
static volatile uint32_t warnings __attribute__((section(".noinit")));
static volatile uint32_t warningsCheck __attribute__((section(".noinit")));

static uint32_t timeoutUs, windowUs;
static uint32_t lastFeed;
static bool enabled;
static char mode = 'n';     // n: feed inside the window, x: stop feeding, f: feed too early

// en: Runs in an interrupt one counter step before the reset; keep it short.
// ja: リセットの1カウント前に割込みの中で呼ばれる。短く保つ。
static void earlyWarning()
{
    warnings = warnings + 1;
    warningsCheck = ~warnings;
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.print("Reset reason: ");
    Serial.println(CH32RV.resetReasonName());
    if (warningsCheck == ~warnings && CH32RV.resetReason() == CH32RVResetReason::WindowWatchdog) {
        Serial.print("Early warnings before the reset: ");
        Serial.println((unsigned long)warnings);
    }
    warnings = 0;
    warningsCheck = ~0u;
    // en: The longest timeout depends on the bus clock; ask the chip rather than assume.
    // ja: 最長のタイムアウトはバスのクロックで決まる。決め打ちせずチップに聞く。
    const uint32_t max = CH32RV.wwdgMaxTimeoutUs();
    timeoutUs = max < 40000u ? max : 40000u;
    windowUs = timeoutUs / 2u;
    Serial.print("Timeout ");
    Serial.print((unsigned long)timeoutUs);
    Serial.print(" us, window opens after ");
    Serial.print((unsigned long)windowUs);
    Serial.println(" us");
    Serial.println("e: enable, x: stop feeding (too late), f: feed too early");
}

void loop()
{
    if (Serial.available()) {
        const int command = Serial.read();
        if (command == 'e' && !enabled) {
            Serial.flush();     // en: no long UART wait once it runs / ja: 動き出したら長いUART待ちをしない
            enabled = CH32RV.wwdgEnable(timeoutUs, windowUs, earlyWarning);
            lastFeed = micros();
            Serial.println(enabled ? "Window watchdog enabled" : "Window watchdog refused");
        } else if ((command == 'x' || command == 'f') && enabled) {
            mode = (char)command;
        }
    }
    if (!enabled || mode == 'x') {
        return;
    }
    // en: Feed three quarters of the way to the timeout: after the window opens, before it ends.
    //     'f' feeds at one eighth instead - before the window - and the chip resets.
    // ja: タイムアウトの3/4で餌: 窓が開いたあと、終わる前。'f'は1/8で餌（窓の前）なのでリセットされる。
    const uint32_t due = mode == 'f' ? timeoutUs / 8u : timeoutUs * 3u / 4u;
    if (micros() - lastFeed >= due) {
        CH32RV.wwdgFeed();
        lastFeed = micros();
    }
}
