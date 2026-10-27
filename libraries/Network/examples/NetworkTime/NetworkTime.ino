// en: Set the clock from NTP and print the local time; say so while it is not set yet.
// ja: NTPで時計を合わせて現地時刻を表示する。合うまでは、まだ合っていないと表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>
#include <time.h>

static const char *TZ_INFO = "JST-9";        // en: POSIX TZ: "UTC0", "CET-1CEST,M3.5.0,M10.5.0/3", ... / ja: POSIXのTZ
static const char *NTP_SERVER = "pool.ntp.org";

static uint32_t lastPrint;

void setup()
{
    Serial.begin(115200);
    delay(100);
    if (!ETH.begin()) {
        Serial.println("ETH.begin() failed");
        return;
    }
    while (!ETH.hasIP()) {
        delay(100);
    }
    // en: Starts SNTP and returns at once; the clock is set when the first answer comes (and every hour after).
    // ja: SNTPを始めてすぐ戻る。時計は最初の応答で合い、その後も1時間ごとに合わせ直す。
    configTzTime(TZ_INFO, NTP_SERVER);
    Serial.println("Waiting for the time...");
}

void loop()
{
    if (millis() - lastPrint < 1000) {
        return;
    }
    lastPrint = millis();
    struct tm now;
    // en: 0 ms: do not wait here; false until the first sync (the clock still says 1970).
    // ja: 0 msで待たない。最初に合うまではfalse（時計はまだ1970年）。
    if (!getLocalTime(&now, 0)) {
        Serial.println(ETH.hasIP() ? "Time not set yet" : "No network: time not set yet");
        return;
    }
    char text[32];
    strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S %Z", &now);
    Serial.println(text);
}
