#include "../../unit/core_redesign/acquisition_buffers.hpp"
using namespace acquisition_experiment;
#if CASE == 2
constexpr unsigned Samples = 8;
#elif CASE == 4
constexpr unsigned Samples = 128;
#else
constexpr unsigned Samples = 64;
#endif
using Pool = Buffers<Samples>;
#if CASE >= 2
Pool pool_probe;
#elif CASE == 1
uint16_t payload_probe[2][64];
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[6], observed[8];
Pool::Token token{};
Pool::Completion completion{};
uint16_t output;
#endif
extern "C" __attribute__((section(".text.entry"), noreturn)) void probe_main() {
    for (;;) {
#if CASE == 1 && !UNUSED
        const unsigned i = argument[0] & 1, j = argument[1] & 63;
        if (operation & 1) payload_probe[i][j] = uint16_t(argument[2]);
        observed[0] = payload_probe[i][j];
#elif CASE >= 2 && !UNUSED
        const Pool::Token incoming{argument[0], uint8_t(argument[1])};
        switch (operation) {
        case 0: observed[0] = pool_probe.open(); break;
        case 1: pool_probe.end(); break;
        case 2: observed[0] = unsigned(pool_probe.reserve(argument[2], token)); break;
        case 3: observed[0] = pool_probe.write(incoming, argument[2], uint16_t(argument[3])); break;
        case 4: observed[0] = pool_probe.cancel(incoming); break;
        case 5: observed[0] = pool_probe.hardware_terminal(incoming, Result(argument[2]), argument[3], argument[4] != 0); break;
        case 6: observed[0] = pool_probe.peek_notice(token); break;
        case 7: observed[0] = pool_probe.ack_notice(incoming); break;
        case 8: observed[0] = pool_probe.take(completion); break;
        case 9: observed[0] = pool_probe.sample(incoming, argument[2], output); break;
        case 10: observed[0] = pool_probe.release(incoming); break;
        }
        observed[1] = token.generation;
        observed[2] = token.index;
        observed[3] = completion.token.generation;
        observed[4] = completion.token.index;
        observed[5] = completion.capture;
        observed[6] = (uint32_t(completion.count) << 8) | unsigned(completion.result);
        observed[7] = output;
#else
        asm volatile("");
#endif
    }
}
