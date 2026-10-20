#include "../../unit/core_redesign/h417_features.hpp"
using namespace h417_feature_experiment;
struct SerialGate {
    bool blocked = false, held = false;
    bool try_enter() { if (blocked || held) return false; held = true; return true; }
    void leave() { held = false; }
};
#if CASE >= 4
SerialGate gate_probe;
constexpr Owners Policy = POLICY == 0 ? Owners::Single : POLICY == 1 ? Owners::Fixed : Owners::Shared;
Fabric<Policy, SerialGate, (FEATURES & 1) != 0, (FEATURES & 2) != 0> fabric_probe(gate_probe, 7);
#elif CASE > 0
constexpr unsigned PayloadBytes = CASE == 1 ? 64 : CASE == 2 ? 128 : 192;
uint8_t payload_probe[PayloadBytes];
#endif
#if CASE > 0 && !UNUSED
volatile uint32_t operation, argument[8], observed[10];
Token token{};
Message message{};
unsigned byte;
#endif
extern "C" __attribute__((section(".text.entry"), noreturn)) void probe_main() {
    for (;;) {
#if CASE > 0 && CASE < 4 && !UNUSED
        unsigned i = argument[0] % PayloadBytes;
        if (operation & 1) payload_probe[i] = uint8_t(argument[1]);
        observed[0] = payload_probe[i];
#elif CASE >= 4 && !UNUSED
        unsigned core = argument[0];
        Token incoming{argument[1], argument[2], argument[3], argument[4]};
        switch (operation) {
        case 0: {
            Message m{{argument[1], argument[2], argument[3], argument[4]}};
            observed[0] = fabric_probe.push(core, m); break;
        }
        case 1: observed[0] = fabric_probe.pop(core, message); break;
        case 2: observed[0] = fabric_probe.reserve(core, token); break;
        case 3: observed[0] = fabric_probe.write(core, incoming, argument[5], argument[6]); break;
        case 4: observed[0] = fabric_probe.publish(core, incoming); break;
        case 5: observed[0] = fabric_probe.take(core, token); break;
        case 6: observed[0] = fabric_probe.read(core, incoming, argument[5], byte); break;
        case 7: observed[0] = fabric_probe.release(core, incoming); break;
        case 8: observed[0] = fabric_probe.ack(core, argument[1]); break;
        case 9: observed[0] = fabric_probe.begin_recovery(core); break;
        case 10: observed[0] = fabric_probe.attest(core, argument[1]); break;
        case 11: observed[0] = fabric_probe.restart(core); break;
        case 12: gate_probe.blocked = core != 0; break;
        }
        observed[1] = token.domain; observed[2] = token.epoch;
        observed[3] = token.generation; observed[4] = token.index;
        for (unsigned i = 0; i < 4; ++i) observed[5+i] = message.words[i];
        observed[9] = byte;
#else
        asm volatile("");
#endif
    }
}
