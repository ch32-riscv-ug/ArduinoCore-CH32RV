// en: Acquire a shared hardware timer and count its update interrupts.
// ja: 共有ハードウェアタイマーを取得し、更新割り込みを数える。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimer.h>

static uint8_t ownerIdentity;
static CH32RVTimerLease lease;
// en: The update ISR increments this count; loop() reads it without doing Serial I/O in the ISR.
// ja: 更新割り込みが回数を増やし、表示はloop()で行う。ISR内でSerialを使わない。
static volatile uint32_t ticks;
static volatile bool running;

static void onUpdate(void *)
{
    ticks++;
}

// en: Called synchronously on loss of ownership; do not re-enter timer management.
// ja: 所有権を失うと同期的に呼ばれる。タイマー管理へ再入しない。
static void onQuiesce(void *, uint8_t, uint8_t)
{
    running = false;
}

static const CH32RVTimerOwner owner = {
    &ownerIdentity, onQuiesce, nullptr
};

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);

    const CH32RVTimerRequest request = {
        CH32RV_TIMER_ANY,
        CH32RV_TIMER_WHOLE,
        {(uint16_t)(F_CPU / 1000000u - 1u), 999u, 0u}
    };

    // en: Use the ownership API before configuring the shared timer.
    // ja: 共有タイマーを設定する前に所有権を取得する。
    lease = ch32TimerTryAcquire(&request, &owner);
    // en: Each setup step must succeed; && stops later steps if an earlier one fails.
    // ja: 各設定の成功を確認する。&&により、途中で失敗すると後続の処理は実行しない。
    running = ch32TimerApplyBase(&lease) &&
              ch32TimerAttachUpdateInterrupt(&lease, onUpdate, nullptr) &&
              ch32TimerStart(&lease);
}

void loop()
{
    static uint32_t last;
    if ((uint32_t)(millis() - last) >= 1000u) {
        last += 1000u;
        Serial.print("TIM lease: ");
        Serial.print(running && ch32TimerLeaseValid(&lease) ? "active, "
                                                            : "lost, ");
        Serial.println(ticks);
    }
}
