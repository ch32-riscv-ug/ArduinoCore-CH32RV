#pragma once
#include "x0_wire.hpp"
#include "i2c_managed_handoff.hpp"

namespace experiment {
// N02 experiment: explicit normal-context polling. No hidden driver work in
// hardware(), start() or take(). Pin readiness and PCLK are declarations only.
template<class Registers> struct X0RegisterWire {
    using Resources = i2c_resource_experiment::Domain<2>;
    using Core = i2c_handoff_experiment::Manager<Registers,3,3,true,41>;
    using Driver = i2c_managed_handoff_experiment::Managed<Core,Resources>;
    struct Pins { bool valid = true; bool ready() const { return valid; } } pins;
    Resources resources;
    Driver driver;
    typename Driver::Storage storage;
    typename Driver::Token request{};
    i2c_experiment::Completion completion{};
    X0WirePacket packet;
    uint8_t owner = 0;
    uint32_t now = 0, due = 0, started = 0, completed = 0, reclaimed = 0, busy = 0, corruptions = 0;
    uint32_t owner_starts[4]{};
    bool initialized = false, active = false, terminal = false, fault = false, virtual_success = true;
    bool initialize(uint32_t hz = 48000000) {
        if (hz < 2000000 || hz > 48000000 || hz%1000000) return false;
        if (initialized || !resources.begin_change() || !resources.publish(hz)) return false;
        for (unsigned i = 0; i < 3; ++i) if (!driver.open(i)) return false;
        initialized = true; return true;
    }
    bool start(uint8_t target, X0WireKind kind, uint8_t address,
               const uint8_t* tx, unsigned tx_size, unsigned rx_size) {
        if (!initialized || !target || target > 3 || tx_size > 33 || rx_size > 8 ||
            (!tx_size && !rx_size) || (tx_size && !tx)) return false;
        if (kind == X0WireKind::I2c) { if (address < 8 || address >= 0x78) return false; }
        else if (kind != X0WireKind::VirtualTouch || target != 3 || address || tx_size != 8 || rx_size) return false;
        if (active) { ++busy; return false; }
        if (kind == X0WireKind::I2c) {
            if (driver.open_backend(resources,pins,400000) != Driver::Open::Ready) return false;
            for (unsigned i = 0; i < tx_size; ++i) if (!storage.write(i,tx[i])) {
                driver.close_backend(); fault = true; return false;
            }
            if (driver.submit(target-1,&storage,tx_size,rx_size,address,request) != i2c_experiment::Start::Accepted) {
                if (!driver.close_backend()) fault = true;
                return false;
            }
        }
        packet.owner = owner = target; packet.address = address; packet.kind = kind;
        packet.tx_size = uint8_t(tx_size); packet.rx_size = uint8_t(rx_size);
        for (unsigned i = 0; i < tx_size; ++i) packet.tx[i] = tx[i];
        virtual_success = true; active = true; terminal = false; due = now+2;
        ++started; ++owner_starts[target]; return true;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && !terminal && packet.kind == X0WireKind::VirtualTouch && now >= due) {
            terminal = true; ++completed;
        }
    }
    // At most one dispatch OR engine step per explicit poll. Reap and backend
    // close are bounded; the outer result/storage loan remains until take.
    void poll() {
        if (!active || terminal || packet.kind != X0WireKind::I2c) return;
        if (driver.phase(request.index) == unsigned(i2c_handoff_experiment::Phase::Queued)) {
            typename Driver::Token dispatched{};
            driver.dispatch(dispatched);
        } else driver.step();
        driver.reap();
        if (driver.notice(request)) {
            if (!driver.close_backend()) { fault = true; return; }
            terminal = true; ++completed;
        }
    }
    bool take(uint8_t target, bool& ok, uint8_t* out, unsigned capacity) {
        if (!active || !terminal || target != owner || capacity < packet.rx_size || (packet.rx_size && !out)) return false;
        bool success = virtual_success;
        if (packet.kind == X0WireKind::I2c) {
            if (!driver.take(request,completion)) return false;
            success = completion.result == i2c_experiment::Result::Success &&
                completion.issued == packet.tx_size && completion.confirmed == packet.tx_size && completion.received == packet.rx_size;
            if (success) for (unsigned i = 0; i < packet.rx_size; ++i) {
                uint8_t value = 0;
                if (!storage.read(packet.tx_size+i,value)) { fault = true; return false; }
                out[i] = value;
            }
        }
        ok = success; active = terminal = false; owner = 0; ++reclaimed; return true;
    }
    bool cancel() {
        if (!active || terminal) return false;
        if (packet.kind == X0WireKind::VirtualTouch) { virtual_success = false; terminal = true; ++completed; return true; }
        return driver.cancel(request) == i2c_experiment::Cancel::Requested;
    }
};
template<unsigned Candidate,class Registers>
using RegisterX0 = FeatureAdapter<Candidate,WireX0Meaning<typename FeatureInbox<Candidate>::Type,X0RegisterWire<Registers>>>;
} // namespace experiment
