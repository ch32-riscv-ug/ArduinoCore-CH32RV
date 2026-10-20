#include "../../unit/core_redesign/work_executor.hpp"

volatile unsigned operation, argument, observation;
#if PART == 1
using Component = experiment::WorkExecutor<CAPACITY>;
Component component;
Component::Token tokens[CAPACITY];
void handler(void*, Component::Token token) {
    observation ^= token.index + 1;
    if (argument & 0x100) component.post(token);
    if (argument & 0x200) component.post(tokens[(token.index + 1) % CAPACITY]);
}
#endif
extern "C" __attribute__((section(".text.entry"))) void data_entry() {
#if PART == 1 && USED
    for (unsigned i = 0; i < CAPACITY; ++i) {
        tokens[i] = component.add(handler, nullptr,
            i % 2 ? experiment::WorkDomain::User : experiment::WorkDomain::System, i % 3 != 0);
    }
    component.initialize();
#endif
    for (;;) {
#if PART == 1 && USED
        if (operation == 1) observation = component.post(tokens[argument % CAPACITY]);
        else {
            constexpr auto opportunity = VARIANT == 0 ? experiment::WorkOpportunity::Explicit :
                                                       experiment::WorkOpportunity::Delay;
            constexpr auto profile = VARIANT == 1 ? experiment::WorkProfile::S :
                                     VARIANT == 2 ? experiment::WorkProfile::U : experiment::WorkProfile::M;
            const auto result = component.turn(WIDTH, opportunity, profile);
            observation = result.selected * 256 + result.delivered;
        }
#else
        observation = argument ^ operation;
#endif
    }
}
