#include "../../unit/core_redesign/io_lifecycle.hpp"

volatile unsigned operation, argument[8], observation;
#if PART != 0
experiment::IoBuffer<CAPACITY> buffers[VARIANT ? 2 : 1];
#endif
#if PART == 2
experiment::IoSlot<CAPACITY> slots[VARIANT ? 2 : 1];
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
    for (;;) {
#if PART == 1 && USED
        uint8_t value = uint8_t(argument[1]);
        if (operation == 1) observation = buffers[0].write(argument[0], value);
        else observation = buffers[0].read(argument[0], value) ? unsigned(value) + 1 : 0;
#elif PART == 2 && USED
        const unsigned index = VARIANT ? argument[0] % 2 : 0;
        auto& slot = slots[index];
        const auto token = argument[1];
        switch (operation % 10) {
        case 0: observation = slot.open(); break;
        case 1: observation = slot.end(); break;
        case 2:
            observation = unsigned(slot.start(&buffers[VARIANT ? argument[2] % 2 : 0],
                argument[3], experiment::IoMode(argument[4]), argument[5]));
            observation ^= slot.token() * 17u; break;
        case 3: observation = unsigned(slot.cancel(token)); break;
        case 4: observation = slot.hardware_terminal(token, experiment::IoResult(argument[2]),
                                                    argument[3], uint8_t(argument[4])); break;
        case 5: {
            experiment::IoSlot<CAPACITY>::Completion completion;
            observation = slot.take(token, completion) ? completion.digest ^ completion.transferred ^
                                                        unsigned(completion.result) : 0;
            break;
        }
        case 6: { experiment::IoSlot<CAPACITY>::Token request = 0; observation = slot.peek_notice(request) ? request : 0; break; }
        case 7: observation = slot.ack_notice(token); break;
        case 8: observation = buffers[index].write(argument[2], uint8_t(argument[3])); break;
        default: { uint8_t value = 0; observation = buffers[index].read(argument[2], value) ? unsigned(value) + 1 : 0; break; }
        }
#else
        observation = argument[0] ^ operation;
#endif
    }
}
