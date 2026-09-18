/* TimerResource - advanced example for library authors.
 *
 * Ordinary sketches should prefer a millis() comparison in loop().  This
 * example demonstrates the lower-level ownership API for code that genuinely
 * needs a TIM interrupt and must coexist with analogWrite(), tone() and Servo.
 */
#include <CH32Timer.h>

static uint8_t ownerIdentity;
static CH32TimerLease lease;
static volatile uint32_t ticks;
static volatile bool running;

static void onUpdate(void *)
{
    ticks++;
}

/* Called synchronously when another API takes this TIM.  It must be short and
 * must not call back into the timer resource API. */
static void onQuiesce(void *, uint8_t, uint8_t)
{
    running = false;
}

static const CH32TimerOwner owner = {
    &ownerIdentity, onQuiesce, nullptr
};

void setup()
{
    Serial.begin(115200);

    /* A 1 us counter tick and a 1 ms update.  This is an API demonstration;
     * millis() is the better solution for ordinary 1 ms application work. */
    const CH32TimerRequest request = {
        CH32_TIMER_ANY,
        CH32_TIMER_WHOLE,
        {(uint16_t)(F_CPU / 1000000u - 1u), 999u, 0u}
    };

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
