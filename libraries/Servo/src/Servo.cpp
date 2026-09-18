#include "Servo.h"

#include "CH32Timer.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

#ifdef CH32_SERVO_TIMER

namespace {

struct Slot {
    uint8_t pin;
    bool active;
    uint16_t pulse_us;
    Servo *owner;
};

Slot slots[CH32_SERVO_MAX];
volatile int8_t current = -1;      /* slot whose pulse is on the wire, or -1 */
volatile uint16_t frame_used_us;   /* how much of the 20 ms frame is spent   */
bool timer_running;
CH32TimerLease timer_lease;
const uint8_t servo_owner_identity = 0;
void timer_update(void *);
void timer_quiesce(void *, uint8_t, uint8_t);
const CH32TimerOwner servo_owner = {
    &servo_owner_identity, timer_quiesce, nullptr
};

/* The timer counts microseconds, so a pulse width is a tick count. The
 * prescaler comes straight from F_CPU because Milestone 1 leaves both APB
 * prescalers at /1. */
inline void timer_set(uint16_t us)
{
    if (us < 2u) {
        us = 2u;                   /* a zero reload would never interrupt */
    }
    /* Only the reload. This runs from the handler, where the counter has just
     * wrapped, so the new period takes effect next time round on its own.
     *
     * It used to fire a software update event here as well, to load the value
     * immediately. That is what timer_start does - but from inside the handler
     * it re-raises the very flag the handler exists to clear, and the core
     * never gets out: measured on CH32V307, the PC was in TIM6_IRQHandler on
     * every sample, UIF was set, and millis() ran at a fifth of real time.
     * wiring_tone.cpp does not hit this because its update event is fired once
     * at start, while the interrupt is still masked. */
    const CH32TimerCapability *cap =
        ch32TimerCapabilityFor(timer_lease.timer);
    if (cap->counter_bits == 32u) {
        CH32_TIM_ATRLR32(cap->register_base) = us - 1u;
    } else {
        CH32_TIM_ATRLR(cap->register_base) = (uint16_t)(us - 1u);
    }
}

void timer_start(void)
{
    if (timer_running && ch32TimerLeaseValid(&timer_lease)) {
        return;
    }
    CH32TimerRequest request = {
        CH32_SERVO_TIMER, CH32_TIMER_WHOLE,
        {(uint16_t)((F_CPU / 1000000u) - 1u), 99u, 0}
    };
    timer_lease = ch32TimerTryAcquire(&request, &servo_owner);
    if (!ch32TimerLeaseValid(&timer_lease)) {
        request.timer = CH32_TIMER_ANY;
        timer_lease = ch32TimerTryAcquire(&request, &servo_owner);
    }
    if (!ch32TimerLeaseValid(&timer_lease)) {
        request.timer = CH32_SERVO_TIMER;
        timer_lease = ch32TimerTakeover(&request, &servo_owner);
    }
    if (!ch32TimerApplyBase(&timer_lease)) {
        timer_quiesce(nullptr, CH32_SERVO_TIMER, CH32_TIMER_WHOLE);
        return;
    }
    current = -1;
    frame_used_us = 0;
    if (!ch32TimerAttachUpdateInterrupt(&timer_lease, timer_update, nullptr) ||
        !ch32TimerStart(&timer_lease)) {
        ch32TimerRelease(&timer_lease);
        timer_quiesce(nullptr, CH32_SERVO_TIMER, CH32_TIMER_WHOLE);
        return;
    }
    timer_running = true;
}

void timer_stop(void)
{
    if (ch32TimerLeaseValid(&timer_lease)) {
        ch32TimerDetachUpdateInterrupt(&timer_lease);
        ch32TimerRelease(&timer_lease);
    }
    timer_running = false;
    current = -1;
}

bool any_active(void)
{
    for (uint8_t i = 0; i < CH32_SERVO_MAX; i++) {
        if (slots[i].active) {
            return true;
        }
    }
    return false;
}

inline void drive(uint8_t pin, bool high)
{
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    const uint8_t bit = (uint8_t)CH32_PIN_BIT(pin);
    if (high) {
        ch32_gpio_set(port, bit);
    } else {
        ch32_gpio_clear(port, bit);
    }
}

void timer_quiesce(void *, uint8_t, uint8_t)
{
    for (uint8_t i = 0; i < CH32_SERVO_MAX; i++) {
        if (slots[i].active) {
            drive(slots[i].pin, false);
            slots[i].active = false;
        }
    }
    timer_running = false;
    current = -1;
    frame_used_us = 0;
    timer_lease = {};
}

}  // namespace

/* One step of the frame: end the pulse that was running, start the next one,
 * and when they are all done wait out whatever is left of the 20 ms. */
namespace {
void timer_update(void *)
{
    if (current >= 0) {
        drive(slots[current].pin, false);
    }

    int8_t next = (int8_t)(current + 1);
    while (next < (int8_t)CH32_SERVO_MAX && !slots[next].active) {
        next++;
    }

    if (next < (int8_t)CH32_SERVO_MAX) {
        current = next;
        const uint16_t us = slots[next].pulse_us;
        drive(slots[next].pin, true);
        frame_used_us = (uint16_t)(frame_used_us + us);
        timer_set(us);
    } else {
        /* Frame over. The gap is what is left of 20 ms; if the pulses somehow
         * filled it, give the line a short break rather than none at all. */
        const uint16_t used = frame_used_us;
        current = -1;
        frame_used_us = 0;
        timer_set(used < (REFRESH_INTERVAL - 100)
                  ? (uint16_t)(REFRESH_INTERVAL - used) : 100u);
    }
}
}  // namespace

Servo::Servo() : _index(INVALID_SERVO), _min(MIN_PULSE_WIDTH),
                 _max(MAX_PULSE_WIDTH)
{
}

uint8_t Servo::attach(int pin)
{
    return attach(pin, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
}

uint8_t Servo::attach(int pin, int min, int max)
{
    if (pin < 0 || !digitalPinIsValid((uint8_t)pin)) {
        return INVALID_SERVO;
    }
    if (_index != INVALID_SERVO && slots[_index].owner != this) {
        /* A TIM takeover detached this object and the slot has since been
         * reused.  Never overwrite the new owner's pulse. */
        _index = INVALID_SERVO;
    }
    if (_index == INVALID_SERVO) {
        for (uint8_t i = 0; i < CH32_SERVO_MAX; i++) {
            if (!slots[i].active) {
                _index = i;
                break;
            }
        }
    }
    if (_index == INVALID_SERVO) {
        return INVALID_SERVO;
    }

    _min = (int16_t)min;
    _max = (int16_t)max;

    const uint8_t port = (uint8_t)CH32_PIN_PORT((uint8_t)pin);
    ch32_gpio_clock_enable(port);
    ch32_gpio_set_config(port, (uint8_t)CH32_PIN_BIT((uint8_t)pin),
                         CH32_GPIO_CFG_OUT_PP_10M);
    drive((uint8_t)pin, false);

    slots[_index].pin = (uint8_t)pin;
    slots[_index].pulse_us = DEFAULT_PULSE_WIDTH;
    slots[_index].owner = this;
    slots[_index].active = true;
    timer_start();
    if (!timer_running) {
        slots[_index].active = false;
        slots[_index].owner = nullptr;
        _index = INVALID_SERVO;
        return INVALID_SERVO;
    }
    return _index;
}

void Servo::detach()
{
    if (_index == INVALID_SERVO) {
        return;
    }
    if (slots[_index].owner == this) {
        slots[_index].active = false;
        slots[_index].owner = nullptr;
        drive(slots[_index].pin, false);
    }
    _index = INVALID_SERVO;
    if (!any_active()) {
        timer_stop();
    }
}

void Servo::write(int value)
{
    /* The AVR library's rule, kept because sketches rely on it. */
    if (value < MIN_PULSE_WIDTH) {
        if (value < 0) {
            value = 0;
        } else if (value > 180) {
            value = 180;
        }
        value = (int)map(value, 0, 180, _min, _max);
    }
    writeMicroseconds(value);
}

void Servo::writeMicroseconds(int value)
{
    if (_index == INVALID_SERVO || slots[_index].owner != this ||
        !slots[_index].active) {
        return;
    }
    if (value < _min) {
        value = _min;
    } else if (value > _max) {
        value = _max;
    }
    slots[_index].pulse_us = (uint16_t)value;
}

int Servo::readMicroseconds()
{
    return (_index == INVALID_SERVO || slots[_index].owner != this ||
            !slots[_index].active) ? 0 : (int)slots[_index].pulse_us;
}

int Servo::read()
{
    return (int)map(readMicroseconds(), _min, _max, 0, 180);
}

bool Servo::attached()
{
    return _index != INVALID_SERVO && slots[_index].owner == this &&
           slots[_index].active;
}

#else  /* the variant found no timer to spare */

Servo::Servo() : _index(INVALID_SERVO), _min(MIN_PULSE_WIDTH),
                 _max(MAX_PULSE_WIDTH)
{
}

uint8_t Servo::attach(int pin)
{
    (void)pin;
    return INVALID_SERVO;
}

uint8_t Servo::attach(int pin, int min, int max)
{
    (void)pin;
    (void)min;
    (void)max;
    return INVALID_SERVO;
}

void Servo::detach() {}
void Servo::write(int value) { (void)value; }
void Servo::writeMicroseconds(int value) { (void)value; }
int Servo::read() { return 0; }
int Servo::readMicroseconds() { return 0; }
bool Servo::attached() { return false; }

#endif
