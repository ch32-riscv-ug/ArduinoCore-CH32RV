// en: Give a slow timer interrupt a less urgent priority so millis() keeps counting while it runs.
// ja: 遅いタイマー割り込みの優先度を下げ、その処理中もmillis()が進むようにする。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVTimer.h>
#include <ch32rv_priority.h>

static uint8_t ownerIdentity;
static CH32RVTimerLease lease;
static uint32_t timerIrq;
static volatile uint32_t movedMs;   // en: how far millis() moved during the last handler / ja: 直前の処理中にmillis()が進んだ量
static volatile bool fired;

// en: A deliberately slow handler: a busy loop of a few milliseconds.
// ja: わざと遅い割り込み処理: 数ミリ秒の空回り。
static void onUpdate(void *)
{
    const uint32_t before = millis();
    for (volatile uint32_t i = 0; i < F_CPU / 2000u; i++) {
    }
    movedMs = millis() - before;
    fired = true;
}

static void onQuiesce(void *, uint8_t, uint8_t)
{
}

static const CH32RVTimerOwner owner = {&ownerIdentity, onQuiesce, nullptr};

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    while (!Serial) {
    }
    // en: A timer counting at 10 kHz, updating every 0.5 s.
    // ja: 10 kHzで数え、0.5秒ごとに更新するタイマー。
    for (size_t i = 0; i < ch32rv_timer_count(); i++) {
        const CH32RVTimerCapability *cap = ch32rv_timer_capability(i);
        const CH32RVTimerRequest request = {
            cap->number, CH32RV_TIMER_WHOLE,
            {(uint16_t)(ch32rv_timer_clock_hz(cap->number) / 10000u - 1u), 4999u, 0u}};
        lease = ch32rv_timer_try_acquire(&request, &owner);
        if (ch32rv_timer_lease_valid(&lease)) {
            timerIrq = cap->update_irq;
            break;
        }
    }
    if (!ch32rv_timer_apply_base(&lease) || !ch32rv_timer_attach_update_interrupt(&lease, onUpdate, nullptr) ||
        !ch32rv_timer_start(&lease)) {
        Serial.println("no timer free");
    }
    Serial.print("preemption levels: ");
    Serial.println(CH32RV_IRQ_PREEMPT_LEVELS);
}

void loop()
{
    static uint32_t round;
    // en: Every 4 handlers, switch between the core's level (0: SysTick waits for the handler)
    //     and the next one down (1: SysTick interrupts the handler, so millis() moves).
    // ja: 4回ごとに、コアと同じレベル（0: SysTickは処理の終わりを待つ）と1つ下のレベル
    //     （1: SysTickが処理に割り込むので、millis()が進む）を切り替える。
    if (fired) {
        fired = false;
        const unsigned level = (round++ / 4u) % 2u;
        Serial.print("level ");
        Serial.print(ch32rv_irq_get_priority(timerIrq) >> (8 - CH32RV_IRQ_PREEMPT_BITS));
        Serial.print(": millis() moved ");
        Serial.print(movedMs);
        Serial.println(" ms during the handler");
        ch32rv_irq_set_priority(timerIrq, CH32RV_IRQ_PRIORITY(level));
    }
}
