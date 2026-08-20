/* Freestanding C++ runtime bits the compiler emits references to.
 *
 * A sketch with a global object that has a destructor - `String buf;` is the
 * common one - makes GCC register that destructor with __cxa_atexit, which
 * pulls in __dso_handle. Without these the sketch fails to link, so this is
 * not an optional nicety.
 *
 * Global destructors never run here: main() does not return, and there is no
 * shutdown path. Recording them would only cost RAM, so __cxa_atexit accepts
 * and forgets.
 */
#include <stdlib.h>

extern "C" {

void *__dso_handle = nullptr;

int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso)
{
    (void)destructor;
    (void)arg;
    (void)dso;
    return 0;
}

void __cxa_finalize(void *dso)
{
    (void)dso;
}

/* Calling through a pure virtual is a program error; stop rather than run on
 * with a wild jump. */
void __cxa_pure_virtual(void)
{
    for (;;) {
    }
}

void __cxa_deleted_virtual(void)
{
    for (;;) {
    }
}

}  // extern "C"

/* -fno-exceptions still leaves the compiler emitting calls to these for any
 * `delete`. newlib-nano's free() is what they should reach. */
void operator delete(void *ptr) noexcept { free(ptr); }
void operator delete[](void *ptr) noexcept { free(ptr); }
void operator delete(void *ptr, size_t) noexcept { free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { free(ptr); }
