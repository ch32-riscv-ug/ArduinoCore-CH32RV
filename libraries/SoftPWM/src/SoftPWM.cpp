#include "SoftPWM.h"

#include <CH32RVHwTimer.h>

#include "internal/ch32rv_gpio.h"

namespace {

struct Channel {
    int16_t pin;                 /* -1: free */
    uint8_t port, bit;
    uint8_t value;               /* the duty now */
    uint8_t target;              /* where a fade is going */
    uint8_t up, down;            /* steps a period, 0: at once */
    uint8_t polarity;
};

Channel channels[SOFTPWM_MAXCHANNELS];
hw_timer_t *timer;
uint8_t defaultPol = SOFTPWM_NORMAL;
volatile uint8_t tick;

inline void drive(const Channel &c, bool active)
{
    if (active != (c.polarity == SOFTPWM_INVERTED)) {
        ch32rv_gpio_set(c.port, c.bit);
    } else {
        ch32rv_gpio_clear(c.port, c.bit);
    }
}

/* SOFTPWM_FREQ x 256 a second: at step 0 every channel with a duty goes
 * active (and fades move one step), at its duty it goes inactive. */
void onTick()
{
    const uint8_t t = tick++;
    for (Channel &c : channels) {
        if (c.pin < 0) {
            continue;
        }
        if (t == 0) {
            if (c.value < c.target) {
                c.value = c.up && c.target - c.value > c.up ? (uint8_t)(c.value + c.up) : c.target;
            } else if (c.value > c.target) {
                c.value = c.down && c.value - c.target > c.down ? (uint8_t)(c.value - c.down) : c.target;
            }
            drive(c, c.value != 0);
        } else if (t == c.value) {
            drive(c, false);
        }
    }
}

Channel *find(int pin, bool make)
{
    Channel *spare = nullptr;
    for (Channel &c : channels) {
        if (c.pin == pin) {
            return &c;
        }
        if (!spare && c.pin < 0) {
            spare = &c;
        }
    }
    if (!make || !spare) {
        return nullptr;
    }
    *spare = Channel{(int16_t)pin, (uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin), 0, 0, 0, 0, defaultPol};
    drive(*spare, false);
    pinMode(pin, OUTPUT);
    return spare;
}

/* Steps of the duty a period for a full swing in `ms`. */
uint8_t rate(uint16_t ms)
{
    if (!ms) {
        return 0;
    }
    const uint32_t periods = (uint32_t)ms * SOFTPWM_FREQ / 1000u;
    return (uint8_t)(periods >= 255u ? 1u : (255u + periods - 1u) / (periods ? periods : 1u));
}

}  // namespace

bool SoftPWMBegin(uint8_t defaultPolarity)
{
    defaultPol = defaultPolarity;
    if (timer) {
        return true;
    }
    for (Channel &c : channels) {
        c.pin = -1;
    }
    timer = timerBegin(1000000);
    if (!timer) {
        return false;
    }
    timerAttachInterrupt(timer, onTick);
    timerAlarm(timer, (1000000u + SOFTPWM_FREQ * 128u) / (SOFTPWM_FREQ * 256u), true, 0);
    return true;
}

void SoftPWMSet(int pin, uint8_t value, uint8_t hardset)
{
    if (!timer) {
        return;
    }
    if (pin == ALL) {
        for (Channel &c : channels) {
            if (c.pin >= 0) {
                SoftPWMSet(c.pin, value, hardset);
            }
        }
        return;
    }
    Channel *c = find(pin, true);
    if (!c) {
        return;
    }
    noInterrupts();
    c->target = value;
    if (hardset || (!c->up && value > c->value) || (!c->down && value < c->value)) {
        c->value = value;
    }
    interrupts();
}

void SoftPWMSetPercent(int pin, uint8_t percent, uint8_t hardset)
{
    SoftPWMSet(pin, (uint8_t)(((uint16_t)(percent > 100 ? 100 : percent) * 255u + 50u) / 100u), hardset);
}

void SoftPWMEnd(int pin)
{
    if (pin == ALL) {
        for (Channel &c : channels) {
            if (c.pin >= 0) {
                SoftPWMEnd(c.pin);
            }
        }
        if (timer) {
            timerEnd(timer);
            timer = nullptr;
        }
        return;
    }
    Channel *c = find(pin, false);
    if (c) {
        noInterrupts();
        drive(*c, false);
        c->pin = -1;
        interrupts();
    }
}

void SoftPWMSetFadeTime(int pin, uint16_t fadeUpTime, uint16_t fadeDownTime)
{
    if (pin == ALL) {
        for (Channel &c : channels) {
            if (c.pin >= 0) {
                SoftPWMSetFadeTime(c.pin, fadeUpTime, fadeDownTime);
            }
        }
        return;
    }
    Channel *c = find(pin, true);
    if (c) {
        c->up = rate(fadeUpTime);
        c->down = rate(fadeDownTime);
    }
}

void SoftPWMSetPolarity(int pin, uint8_t polarity)
{
    if (pin == ALL) {
        for (Channel &c : channels) {
            if (c.pin >= 0) {
                c.polarity = polarity;
            }
        }
        return;
    }
    Channel *c = find(pin, true);
    if (c) {
        c->polarity = polarity;
    }
}
