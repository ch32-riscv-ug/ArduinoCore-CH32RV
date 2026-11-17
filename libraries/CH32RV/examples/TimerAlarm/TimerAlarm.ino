// en: Run a function on a hardware timer's alarm every second, with arduino-esp32's timer API.
// ja: arduino-esp32のタイマーAPIで、ハードウェアのタイマーのアラームで毎秒関数を動かす。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

static hw_timer_t *timer;
static volatile uint32_t alarms;
static volatile uint64_t lastCount;

// en: Runs in the timer's interrupt: keep it short, and print from loop().
// ja: タイマーの割り込みで動く。短くし、表示はloop()で。
static void onAlarm()
{
    alarms++;
    lastCount = timerRead(timer);
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    // en: The counter counts at 1 MHz (one tick per microsecond) and starts at once.
    // ja: カウンタは1 MHz（1 usに1つ）で数え、すぐに動き出す。
    timer = timerBegin(1000000);
    if (!timer) {
        Serial.println("No timer free");
        return;
    }
    timerAttachInterrupt(timer, onAlarm);
    // en: Alarm at 1000000 ticks, then back to 0 and again: every second.
    // ja: 1000000カウントでアラーム、0に戻って繰り返す: 1秒ごと。
    timerAlarm(timer, 1000000, true, 0);
}

void loop()
{
    static uint32_t seen;
    if (timer && alarms != seen) {
        seen = alarms;
        Serial.print("Alarm ");
        Serial.print(seen);
        Serial.print(", count at it ");
        Serial.print((uint32_t)lastCount);
        Serial.print(", ");
        Serial.print((uint32_t)timerReadMillis(timer));
        Serial.println(" ms into the period");
    }
}
