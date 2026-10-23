// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVTimer.h>

static uint8_t ownerIdentity;
static CH32RVTimerLease lease;
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

    // en: Use the ownership API before configuring the shared timer. / ja: 共有タイマーを設定する前に所有権を取得する。
    lease = ch32TimerTryAcquire(&request, &owner);
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
