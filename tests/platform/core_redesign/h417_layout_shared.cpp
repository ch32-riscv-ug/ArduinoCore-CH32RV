#include <stdint.h>
// Opaque placement budget only. No constructors, pointers, locks or protocol.
extern "C" {
alignas(16) volatile uint8_t experiment_shared[SHARED_BYTES]
    __attribute__((section(".shared_data"), used));
}
