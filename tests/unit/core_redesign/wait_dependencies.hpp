#pragma once
#include "wait_policy.hpp"

namespace experiment {
// Comparison instrumentation, not a product-wide deadlock detector. All
// listed dependencies are conjunctive and describe outstanding acquisitions.
// A complete snapshot is a caller/backend assertion, not inferred by this type.
enum class DependencyCheck { NotChecked, NoKnownBlock, HeldByWaiter, Unknown,
                             ScopeExcluded, Cycle, Invalid };
template<unsigned Actors, unsigned Resources> struct WaitDependencies {
    static_assert(Actors > 0 && Actors <= 16 && Resources > 0 && Resources <= 16, "bounded graph");
    struct Actor {
        uint32_t needs = 0, waits_for = 0;
        WaitProducer producer = WaitProducer::System;
        bool opted = false, known = true;
    } actors[Actors]{};
    static constexpr int8_t Free = -1, Opaque = -2;
    int8_t owners[Resources]{};
    constexpr WaitDependencies() { for (auto& owner : owners) owner = Free; }
    DependencyCheck inspect(unsigned producer, unsigned waiter, const WaitPermissions& permission) const {
        if (producer >= Actors || waiter >= Actors) return DependencyCheck::Invalid;
        constexpr uint32_t actor_mask = (1u << Actors) - 1, resource_mask = (1u << Resources) - 1;
        uint32_t edges[Actors]{}, unknown = 0, excluded = 0, invalid = 0;
        for (unsigned i = 0; i < Actors; ++i) {
            const auto& actor = actors[i];
            const auto bit = 1u << i;
            edges[i] = actor.waits_for & actor_mask;
            if ((actor.waits_for & ~actor_mask) || (actor.needs & ~resource_mask)) invalid |= bit;
            if (!actor.known) unknown |= bit;
            if (unsigned(actor.producer) > unsigned(WaitProducer::OtherCore)) invalid |= bit;
            auto p = permission; p.user_opted = actor.opted;
            if (!p.supports(actor.producer)) excluded |= bit;
            for (unsigned r = 0; r < Resources; ++r) {
                if (!(actor.needs & (1u << r))) continue;
                const int owner = owners[r];
                if (owner == Opaque) unknown |= bit;
                else if (owner != Free) {
                    if (owner < 0 || unsigned(owner) >= Actors) invalid |= bit;
                    else edges[i] |= 1u << unsigned(owner);
                }
            }
        }
        // Finite transitive closure; repeated convergence/recursive calls are
        // unnecessary. Unrelated cycles and diamond-shaped paths are distinct.
        for (unsigned k = 0; k < Actors; ++k)
            for (unsigned i = 0; i < Actors; ++i)
                if (edges[i] & (1u << k)) edges[i] |= edges[k];
        const uint32_t reachable = (1u << producer) | edges[producer];
        if (reachable & invalid) return DependencyCheck::Invalid;
        if (reachable & (1u << waiter)) return DependencyCheck::HeldByWaiter;
        if (reachable & unknown) return DependencyCheck::Unknown;
        if (reachable & excluded) return DependencyCheck::ScopeExcluded;
        for (unsigned i = 0; i < Actors; ++i)
            if ((reachable & (1u << i)) && (edges[i] & (1u << i))) return DependencyCheck::Cycle;
        return DependencyCheck::NoKnownBlock;
    }
};
// Guard a Drive passed to the existing wait_for. The wrapper still performs
// immediate try/context/timeout/clock/scope checks first. No automatic unlock,
// cancellation, backend pumping or extra scheduler is added by the guard.
template<class Graph, class Drive> struct DependencyDrive {
    const Graph& graph;
    const WaitPermissions& permissions;
    unsigned producer, waiter;
    Drive drive;
    DependencyCheck last = DependencyCheck::NotChecked;
    unsigned checks = 0;
    bool operator()() {
        ++checks; last = graph.inspect(producer, waiter, permissions);
        return last == DependencyCheck::NoKnownBlock && drive();
    }
};
} // namespace experiment
