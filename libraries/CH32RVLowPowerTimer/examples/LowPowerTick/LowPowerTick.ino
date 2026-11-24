// en: Sleep in lightSleep() and let the low-power timer (LPTIM on LSI) wake the chip four times a second.
// ja: lightSleep() で眠り、低消費電力タイマー（LSI の LPTIM）が 1 秒に 4 回起こす。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: LOW_POWER_TIMER */

#include <CH32RV.h>
#include <CH32RVLowPowerTimer.h>

CH32RVLowPowerTimer lpt;             // en: LPTIM1 / ja: LPTIM1
static volatile uint32_t ticks;

static void onTick()
{
    ticks++;                          // en: in the timer's interrupt: keep it short / ja: タイマーの割り込みの中。短く
}

void setup()
{
    Serial.begin(115200);
    // en: LSI / 32: about 1.2 kHz, and it keeps running while the chip sleeps
    // ja: LSI / 32: 約 1.2 kHz。石が眠っている間も動き続ける
    if (!lpt.begin(CH32RVLowPowerTimerClock::Lsi, 32)) {
        Serial.println("LPTIM begin() failed");
        return;
    }
    lpt.setPeriod(lpt.clockHz() / 4);   // en: a period of 250 ms / ja: 250 ms の周期
    lpt.onPeriod(onTick);
    // en: the timer's wake line ends lightSleep(); without it only another wake-up would
    // ja: タイマーの起床の線で lightSleep() が終わる。無いと別の起床まで眠ったまま
    CH32RV.enableExtiLineWakeup(lpt.wakeLine());
    lpt.start();
    Serial.printf("LPTIM at %lu Hz (LSI is only roughly its nominal rate)\n", (unsigned long)lpt.clockHz());
}

void loop()
{
    Serial.flush();                   // en: let the last line out before the clocks stop / ja: クロックが止まる前に送り切る
    CH32RV.lightSleep();
    Serial.printf("woke, tick %lu\n", (unsigned long)ticks);
}
