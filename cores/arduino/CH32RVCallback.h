/* The callback type of the USB classes: a function pointer, or a lambda that
 * captures a little - `[this]`, a pointer and a flag - as EspUsbDevice's
 * std::function callbacks are used (OEP's DirectBulkStream registers
 * `[this](size_t n) { ... }`). Held in place, never on the heap, and as cheap
 * to call as a function pointer plus one indirect call.
 *
 * What it takes: anything callable as R(Args...) that is trivially copyable
 * and at most two pointers big - a capture list of references, pointers and
 * small values. A bigger or non-trivial capture is a compile error pointing
 * here, not a silent heap allocation. */
#pragma once

#include <new>
#include <stddef.h>
#include <string.h>
#include <type_traits>

template <typename Signature> class CH32RVCallback;

template <typename R, typename... Args> class CH32RVCallback<R(Args...)> {
public:
    constexpr CH32RVCallback() = default;
    constexpr CH32RVCallback(std::nullptr_t) {}
    CH32RVCallback(R (*fn)(Args...))
    {
        if (fn) {
            assign(fn);
        }
    }
    template <typename Fn, typename = typename std::enable_if<
                              !std::is_same<typename std::decay<Fn>::type, CH32RVCallback>::value &&
                              !std::is_pointer<typename std::decay<Fn>::type>::value &&
                              !std::is_same<typename std::decay<Fn>::type, std::nullptr_t>::value>::type>
    CH32RVCallback(Fn f)
    {
        assign(f);
    }

    explicit operator bool() const { return _call != nullptr; }
    R operator()(Args... args) const { return _call(_store, static_cast<Args>(args)...); }

private:
    template <typename Fn> void assign(Fn f)
    {
        static_assert(sizeof(Fn) <= sizeof(_store), "CH32RVCallback: the capture is bigger than two pointers");
        static_assert(alignof(Fn) <= alignof(void *), "CH32RVCallback: the capture is over-aligned");
        static_assert(std::is_trivially_copyable<Fn>::value,
                      "CH32RVCallback: capture only references, pointers and small values");
        ::new (static_cast<void *>(_store)) Fn(f);
        _call = [](const void *store, Args... args) -> R {
            return (*static_cast<const Fn *>(store))(static_cast<Args>(args)...);
        };
    }

    alignas(void *) unsigned char _store[2 * sizeof(void *)] = {};
    R (*_call)(const void *, Args...) = nullptr;
};
