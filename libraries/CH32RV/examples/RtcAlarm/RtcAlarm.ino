// en: Keep the date in the RTC, ring an alarm every 10 seconds, sleep until it, and count boots in a backup register.
// ja: RTCで日時を保ち、10秒ごとにアラームを鳴らし、アラームまで眠り、起動の回数をバックアップレジスタで数える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RV.h>
#include <time.h>
#include <sys/time.h>

static volatile bool rang;

// en: Runs in the RTC alarm's interrupt: only note it, and do the work in loop().
// ja: RTCのアラームの割込みの中で呼ばれる。印だけ付け、仕事はloop()でする。
static void onAlarm()
{
    rang = true;
}

static void printNow()
{
    const time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    char text[32];
    strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    Serial.println(text);
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    Serial.print("Reset reason: ");
    Serial.print(CH32RV.resetReasonName());
    Serial.print(", wakeup cause: ");
    Serial.println((int)CH32RV.wakeupCause());

    // en: Backup word 0 counts the boots; it survives resets and standby (and a power cut with a VBAT battery,
    //     where the words are BKP registers - backupRegistersKeepPower()).
    // ja: バックアップの語0で起動を数える。リセットとスタンバイをまたいで残る（BKPのレジスタならVBATの電池で
    //     電源断も。backupRegistersKeepPower()）。
    if (CH32RV.backupRegisterCount() > 0) {
        const uint16_t boots = CH32RV.getBackupRegister(0) + 1u;
        CH32RV.setBackupRegister(0, boots);
        Serial.print("Boot number ");
        Serial.println(boots);
    } else {
        Serial.println("No backup registers on this chip");
    }

    // en: The RTC starts at 1970 after a power cut; set a date with settimeofday() (or SNTP's configTime()).
    // ja: 電源断のあとRTCは1970から。日時はsettimeofday()（かSNTPのconfigTime()）で合わせる。
    if (time(nullptr) < 1700000000) {
        struct tm start = {};
        start.tm_year = 2026 - 1900;
        start.tm_mon = 0;
        start.tm_mday = 1;
        const struct timeval tv = {mktime(&start), 0};
        settimeofday(&tv, nullptr);
    }
    printNow();
    if (!CH32RV.setAlarm(time(nullptr) + 10, onAlarm)) {
        Serial.println("No RTC alarm on this chip");
    }
    Serial.println("s: sleep until the next alarm");
}

void loop()
{
    if (rang) {
        rang = false;
        Serial.print("Alarm at ");
        printNow();
        // en: One alarm at a time: set the next one here.
        // ja: アラームは1つずつ。次をここで設定する。
        CH32RV.setAlarm(time(nullptr) + 10, onAlarm);
    }
    if (Serial.available() && Serial.read() == 's' && CH32RV.getAlarm() != 0) {
        Serial.println("Sleeping until the alarm");
        CH32RV.disableWakeup();
        CH32RV.enableAlarmWakeup();
        const CH32RVSleepResult result = CH32RV.lightSleep();
        CH32RV.disableWakeup();
        Serial.print("Woke: result ");
        Serial.print((int)result);
        Serial.print(", cause ");
        Serial.println((int)CH32RV.wakeupCause());
    }
}
