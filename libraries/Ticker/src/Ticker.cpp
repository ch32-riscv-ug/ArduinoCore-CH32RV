#include "Ticker.h"

#include "internal/ch32rv_irq.h"

namespace {

hw_timer_t *timer;          /* shared by every started Ticker; 1 MHz */
Ticker *started;            /* the list (Ticker::_next) */

}  // namespace

Ticker::Ticker() {}

Ticker::~Ticker()
{
    detach();
}

void Ticker::_static_callback(void *arg)
{
    Ticker *self = static_cast<Ticker *>(arg);
    if (self->_callback_function) {
        self->_callback_function();
    }
}

/* The alarm at the earliest due Ticker; the timer back when none is left.
 * Interrupts held by the caller. */
void Ticker::_schedule()
{
    if (!started) {
        timerEnd(timer);
        timer = nullptr;
        return;
    }
    uint64_t due = started->_due;
    for (Ticker *t = started->_next; t; t = t->_next) {
        if (t->_due < due) {
            due = t->_due;
        }
    }
    timerAlarm(timer, due, false, 0);
}

/* The timer's alarm: every Ticker that is due, one at a time from the start
 * of the list - a callback may attach or detach any of them. */
void Ticker::_service(void *)
{
    for (;;) {
        const uint64_t now = timerRead(timer);
        Ticker *t = started;
        while (t && t->_due > now) {
            t = t->_next;
        }
        if (!t) {
            break;
        }
        const callback_with_arg_t callback = t->_callback;
        void *arg = t->_arg;
        if (t->_period) {
            t->_due += t->_period;
            if (t->_due <= now) {
                t->_due = now + t->_period;      /* a whole period missed: skip it */
            }
        } else {
            t->detach();
        }
        callback(arg);
        if (!timer) {
            return;                              /* the last Ticker stopped */
        }
    }
    _schedule();
}

void Ticker::_attach_us(uint64_t micros, bool repeat, callback_with_arg_t callback, void *arg)
{
    detach();
    if (micros == 0u || !callback) {
        return;                    /* no period to keep */
    }
    const uint32_t irq = ch32rv_irq_save();
    if (!timer) {
        timer = timerBegin(1000000);
        if (!timer) {
            ch32rv_irq_restore(irq);
            return;
        }
        timerAttachInterruptArg(timer, _service, nullptr);
    }
    _callback = callback;
    _arg = arg;
    _period = repeat ? micros : 0u;
    _due = timerRead(timer) + micros;
    _next = started;
    started = this;
    _active = true;
    _schedule();
    ch32rv_irq_restore(irq);
}

void Ticker::detach()
{
    const uint32_t irq = ch32rv_irq_save();
    if (_active) {
        _active = false;
        for (Ticker **p = &started; *p; p = &(*p)->_next) {
            if (*p == this) {
                *p = _next;
                break;
            }
        }
        _next = nullptr;
        _schedule();
    }
    ch32rv_irq_restore(irq);
}
