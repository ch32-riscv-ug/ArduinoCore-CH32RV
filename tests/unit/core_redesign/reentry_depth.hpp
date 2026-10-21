#pragma once
#include "wait_policy.hpp"

namespace experiment {
enum class DepthOutcome : uint8_t {
    Completed, Pending, DepthLimit, Active, ResourceBusy, HeldByActive,
    ScopeExcluded, UserNestedForbidden, Forbidden, Invalid
};
// Artificial finite dependency-pump comparison, not a replacement executor or
// an adopted blocking API. A synchronous attempt retains each caller frame;
// the continuation form returns Pending instead of entering its producer.
template<unsigned Limit,bool Nested> struct ReentryDepth {
    static_assert(Limit==1 || Limit==2 || Limit==4,"fixed comparison depths");
    static constexpr unsigned Count=5;
    struct Node {
        int8_t dependency=-1,needs=-1,holds=-1;
        WaitProducer kind=WaitProducer::System;
        bool opted=false;
    } nodes[Count]{};
    int8_t owner[2]{-1,-1};
    WaitPermissions permission{};
    uint8_t done=0,active=0,depth=0,peak=0;
    uint32_t calls=0,limited=0,active_hits=0,held_hits=0;
    bool configured() const {
        for (const auto& node:nodes)
            if (node.dependency < -1 || node.dependency>=int(Count) ||
                node.needs < -1 || node.needs>=2 || node.holds < -1 || node.holds>=2 ||
                unsigned(node.kind)>unsigned(WaitProducer::OtherCore)) return false;
        for (int value:owner) if (value < -2 || value>=int(Count)) return false;
        return true;
    }
    DepthOutcome invoke(unsigned index) {
        if (done & (1u<<index)) return DepthOutcome::Completed;
        if (active & (1u<<index)) { ++active_hits; return DepthOutcome::Active; }
        const auto& node=nodes[index];
        auto p=permission;p.user_opted=node.opted;
        if (!p.supports(node.kind)) return DepthOutcome::ScopeExcluded;
        if (node.kind==WaitProducer::Interrupt || node.kind==WaitProducer::OtherCore)
            return DepthOutcome::Pending; // External completion requires an explicit stimulus.
        if (depth && node.kind==WaitProducer::User) return DepthOutcome::UserNestedForbidden;
        if (depth>=Limit) { ++limited; return DepthOutcome::DepthLimit; }
        const int resources[2]{node.needs,node.holds};
        for (int resource:resources) {
            if (resource < 0) continue;
            const int holder=owner[resource];
            if (holder>=0 && (active & (1u<<unsigned(holder)))) {
                ++held_hits;return DepthOutcome::HeldByActive;
            }
            if (holder!=-1) return DepthOutcome::ResourceBusy;
        }
        if (node.holds>=0) owner[unsigned(node.holds)]=int8_t(index);
        active=uint8_t(active|(1u<<index));++depth;++calls;
        if (depth>peak) peak=depth;
        DepthOutcome result=DepthOutcome::Completed;
        if (node.dependency>=0 && !(done & (1u<<unsigned(node.dependency)))) {
            if constexpr (Nested) result=invoke(unsigned(node.dependency));
            else result=DepthOutcome::Pending;
        }
        if (result==DepthOutcome::Completed) done=uint8_t(done|(1u<<index));
        --depth;active=uint8_t(active & ~(1u<<index));
        if (node.holds>=0) owner[unsigned(node.holds)]=-1;
        return result;
    }
    DepthOutcome run(unsigned index,WorkContext context=WorkContext::Normal) {
        if (context!=WorkContext::Normal) return DepthOutcome::Forbidden;
        if (index>=Count || !configured()) return DepthOutcome::Invalid;
        return invoke(index);
    }
    bool external(unsigned index) {
        if (index>=Count) return false;
        const auto kind=nodes[index].kind;
        if (kind!=WaitProducer::Interrupt && kind!=WaitProducer::OtherCore) return false;
        auto p=permission;p.user_opted=nodes[index].opted;
        if (!p.supports(kind)) return false;
        done=uint8_t(done|(1u<<index));return true;
    }
};
} // namespace experiment
