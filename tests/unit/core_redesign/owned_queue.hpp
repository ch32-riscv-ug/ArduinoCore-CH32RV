#pragma once
#include <new>
#include <type_traits>
#include <utility>

namespace experiment {
// Normal-context comparison only. Constructors/destructors are user code;
// their duration and effects are not bounded by this container.
template<class T, unsigned N>
class OwnedQueue {
    static_assert(N > 0 && N <= 65535, "bounded capacity");
    static_assert(std::is_nothrow_move_constructible<T>::value &&
                  std::is_nothrow_move_assignable<T>::value &&
                  std::is_nothrow_destructible<T>::value, "noexcept lifetime operations required");
    struct Slot { alignas(T) unsigned char bytes[sizeof(T)]; } slots[N];
    unsigned head = 0, count = 0;
    T* object(unsigned index) { return std::launder(reinterpret_cast<T*>(slots[index].bytes)); }
public:
    OwnedQueue() noexcept = default;
    OwnedQueue(const OwnedQueue&) = delete;
    OwnedQueue& operator=(const OwnedQueue&) = delete;
    ~OwnedQueue() noexcept { clear(); }
    unsigned size() const noexcept { return count; }
    template<class... Args> bool emplace(Args&&... args) noexcept {
        static_assert(std::is_nothrow_constructible<T, Args...>::value, "noexcept construction required");
        if (count == N) return false;
        ::new (static_cast<void*>(slots[(head + count) % N].bytes)) T(std::forward<Args>(args)...);
        ++count;
        return true;
    }
    bool push(T&& value) noexcept { return emplace(std::move(value)); }
    bool pop(T& value) noexcept {
        if (!count) return false;
        value = std::move(*object(head));
        object(head)->~T();
        head = (head + 1) % N; --count;
        return true;
    }
    void clear() noexcept {
        while (count) {
            object(head)->~T(); head = (head + 1) % N; --count;
        }
    }
};
} // namespace experiment
