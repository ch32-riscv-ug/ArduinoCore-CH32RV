#include <stdint.h>
extern "C" {
extern volatile uint8_t experiment_shared[];
extern uint8_t _sbss[], _ebss[], _data_vma[], _edata[], _data_lma[];
extern uint8_t _susrstack[], _eusrstack[];
volatile uint32_t private_data = 0x10203040u + CORE;
volatile uint32_t private_bss[13 + PRIVATE_WORDS];
// Link probe only: does not initialize RAM or configure a runnable H417 core.
__attribute__((section(".init"), noreturn)) void _start() {
    for (;;) {
        private_bss[0] = uint32_t(uintptr_t(_sbss) ^ uintptr_t(_ebss) ^
            uintptr_t(_data_vma) ^ uintptr_t(_edata) ^ uintptr_t(_data_lma) ^
            uintptr_t(_susrstack) ^ uintptr_t(_eusrstack)) ^ private_data ^ experiment_shared[0];
    }
}
}
