#include <stdint.h>
extern "C" {
#if SHARED_ACTIVE
extern volatile uint8_t experiment_shared[];
#endif
extern uint8_t _sbss[], _ebss[], _data_vma[], _edata[], _data_lma[];
extern uint8_t _susrstack[], _eusrstack[];
volatile uint32_t private_data = 0x10203040u + CORE;
volatile uint32_t private_bss[13];
__attribute__((section(".init"), noreturn)) void _start() {
    for (;;) {
        uint32_t observed = uint32_t(uintptr_t(_sbss) ^ uintptr_t(_ebss) ^
            uintptr_t(_data_vma) ^ uintptr_t(_edata) ^ uintptr_t(_data_lma) ^
            uintptr_t(_susrstack) ^ uintptr_t(_eusrstack)) ^ private_data;
#if SHARED_ACTIVE
        observed ^= experiment_shared[0];
#endif
        private_bss[0] = observed;
    }
}
}
