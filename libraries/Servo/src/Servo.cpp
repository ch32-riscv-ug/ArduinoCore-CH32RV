#include "Servo.h"

#include "ch32_gpio.h"
#include "ch32_registers.h"

#ifdef CH32_SERVO_TIMER

namespace {

struct Slot {
    uint8_t pin;
    bool active;
    uint16_t pulse_us;
};

Slot slots[CH32_SERVO_MAX];
volatile int8_t current = -1;      /* slot whose pulse is on the wire, or -1 */
volatile uint16_t frame_used_us;   /* how much of the 20 ms frame is spent   */
bool timer_running;

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
    CH32_TIM_ATRLR(CH32_SERVO_TIMER_BASE) = (uint16_t)(us - 1u);
}

void timer_start(void)
{
    if (timer_running) {
        return;
    }
#if CH32_SERVO_TIMER_ON_APB2
    CH32_RCC_APB2PCENR |= CH32_SERVO_TIMER_RCC;
#else
    CH32_RCC_APB1PCENR |= CH32_SERVO_TIMER_RCC;
#endif
    CH32_TIM_CTLR1(CH32_SERVO_TIMER_BASE) = 0;
    CH32_TIM_PSC(CH32_SERVO_TIMER_BASE) = (uint16_t)((F_CPU / 1000000u) - 1u);
    current = -1;
    frame_used_us = 0;
    timer_set(100);                /* first interrupt starts the frame */
    /* Load PSC and ATRLR now and drop the update flag that loading them
     * raises, while the interrupt is still masked. */
    CH32_TIM_SWEVGR(CH32_SERVO_TIMER_BASE) = CH32_TIM_SWEVGR_UG;
    CH32_TIM_INTFR(CH32_SERVO_TIMER_BASE) = 0;
    CH32_TIM_DMAINTENR(CH32_SERVO_TIMER_BASE) = CH32_TIM_INT_UIE;
    ch32_irq_enable(CH32_SERVO_TIMER_IRQ);
    CH32_TIM_CTLR1(CH32_SERVO_TIMER_BASE) = CH32_TIM_CTLR1_CEN;
    timer_running = true;
}

void timer_stop(void)
{
    CH32_TIM_CTLR1(CH32_SERVO_TIMER_BASE) = 0;
    CH32_TIM_DMAINTENR(CH32_SERVO_TIMER_BASE) = 0;
    ch32_irq_disable(CH32_SERVO_TIMER_IRQ);
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

}  // namespace

/* One step of the frame: end the pulse that was running, start the next one,
 * and when they are all done wait out whatever is left of the 20 ms. */
extern "C" __attribute__((interrupt)) void CH32_SERVO_TIMER_HANDLER(void)
{
    CH32_TIM_INTFR(CH32_SERVO_TIMER_BASE) = (uint16_t)~CH32_TIM_INT_UIE;

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
    slots[_index].active = true;
    timer_start();
    return _index;
}

void Servo::detach()
{
    if (_index == INVALID_SERVO) {
        return;
    }
    slots[_index].active = false;
    drive(slots[_index].pin, false);
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
    if (_index == INVALID_SERVO) {
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
    return _index == INVALID_SERVO ? 0 : (int)slots[_index].pulse_us;
}

int Servo::read()
{
    return (int)map(readMicroseconds(), _min, _max, 0, 180);
}

bool Servo::attached()
{
    return _index != INVALID_SERVO && slots[_index].active;
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
