#pragma once
// Link/assembly experiment, not a validated hardware backend. Masks and the
// X035 fence.i follow fixed WCH SDK sources cited in shared-result.ja.md.
// Caller must be on the same core and must not modify this mask in the scope.
namespace experiment {
template<unsigned Csr> struct QingkeIrqGate {
    using Token = unsigned;
    bool try_enter(Token& saved) {
        const unsigned mask = 0x88;
        asm volatile("csrrc %0, %1, %2" : "=r"(saved) : "i"(Csr), "r"(mask) : "memory");
        if constexpr (Csr == 0x800) asm volatile("fence.i" ::: "memory");
        saved &= mask;
        return true;
    }
    void leave(Token saved) {
        const unsigned mask = 0x88;
        asm volatile("csrc %0, %1" :: "i"(Csr), "r"(mask) : "memory");
        asm volatile("csrs %0, %1" :: "i"(Csr), "r"(saved) : "memory");
    }
};
} // namespace experiment
