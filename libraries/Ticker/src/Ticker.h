/* Ticker: call a function every so often, or once after a while - arduino-
 * esp32's Ticker API.
 *
 *     Ticker blink;
 *     blink.attach_ms(500, [] { digitalWrite(LED, !digitalRead(LED)); });
 *
 * Every Ticker of the sketch shares one hardware timer (timerBegin() of
 * CH32RVHwTimer.h, at 1 MHz): it is taken when the first Ticker starts and
 * given back when the last one stops. The callback runs in that timer's
 * interrupt (on the ESP32 it runs in the esp_timer task): keep it short, and
 * leave delay(), Serial output and the like to loop(). A Ticker may attach,
 * detach or restart itself or another from its callback.
 *
 * Times are in microseconds on the timer's 64-bit count: a periodic Ticker
 * keeps its period from the first call on (no drift), and a call that comes
 * late does not move the next one; a late call missed a whole period is
 * skipped. When no timer is free (analogWrite(), tone(), Servo or a library
 * hold them all), attach*() and once*() do nothing and active() is false. */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>

class Ticker {
public:
    Ticker();
    ~Ticker();

    typedef void (*callback_with_arg_t)(void *);
    /* A function, or a lambda capturing at most two pointers (CH32RVCallback.h). */
    typedef CH32RVCallback<void()> callback_function_t;

    void attach(float seconds, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us((uint64_t)(1000000.0f * seconds), true, _static_callback, this);
    }
    void attach_ms(uint64_t milliseconds, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us(1000ULL * milliseconds, true, _static_callback, this);
    }
    void attach_us(uint64_t micros, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us(micros, true, _static_callback, this);
    }

    template <typename TArg> void attach(float seconds, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "attach() callback argument size must be <= sizeof(void*)");
        _attach_us((uint64_t)(1000000.0f * seconds), true, reinterpret_cast<callback_with_arg_t>(callback),
                   (void *)arg);
    }
    template <typename TArg> void attach_ms(uint64_t milliseconds, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "attach_ms() callback argument size must be <= sizeof(void*)");
        _attach_us(1000ULL * milliseconds, true, reinterpret_cast<callback_with_arg_t>(callback), (void *)arg);
    }
    template <typename TArg> void attach_us(uint64_t micros, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "attach_us() callback argument size must be <= sizeof(void*)");
        _attach_us(micros, true, reinterpret_cast<callback_with_arg_t>(callback), (void *)arg);
    }

    void once(float seconds, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us((uint64_t)(1000000.0f * seconds), false, _static_callback, this);
    }
    void once_ms(uint64_t milliseconds, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us(1000ULL * milliseconds, false, _static_callback, this);
    }
    void once_us(uint64_t micros, callback_function_t callback)
    {
        _callback_function = callback;
        _attach_us(micros, false, _static_callback, this);
    }

    template <typename TArg> void once(float seconds, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "once() callback argument size must be <= sizeof(void*)");
        _attach_us((uint64_t)(1000000.0f * seconds), false, reinterpret_cast<callback_with_arg_t>(callback),
                   (void *)arg);
    }
    template <typename TArg> void once_ms(uint64_t milliseconds, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "once_ms() callback argument size must be <= sizeof(void*)");
        _attach_us(1000ULL * milliseconds, false, reinterpret_cast<callback_with_arg_t>(callback), (void *)arg);
    }
    template <typename TArg> void once_us(uint64_t micros, void (*callback)(TArg), TArg arg)
    {
        static_assert(sizeof(TArg) <= sizeof(void *), "once_us() callback argument size must be <= sizeof(void*)");
        _attach_us(micros, false, reinterpret_cast<callback_with_arg_t>(callback), (void *)arg);
    }

    void detach();
    bool active() const { return _active; }

protected:
    static void _static_callback(void *arg);
    callback_function_t _callback_function = nullptr;

private:
    void _attach_us(uint64_t micros, bool repeat, callback_with_arg_t callback, void *arg);
    static void _service(void *);
    static void _schedule();

    Ticker *_next = nullptr;          /* the list of started Tickers */
    uint64_t _due = 0;                /* the timer's count of the next call */
    uint64_t _period = 0;             /* 0: once */
    callback_with_arg_t _callback = nullptr;
    void *_arg = nullptr;
    volatile bool _active = false;
};
