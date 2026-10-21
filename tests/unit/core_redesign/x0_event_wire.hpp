#pragma once
#include "x0_register_wire.hpp"
#include "completion_delivery.hpp"

namespace experiment {
// N02 experiment: a source-scoped hint gates explicit application take.
// The internal handler never calls the application, polls hardware, acknowledges
// the source, or returns its storage. Source/context must outlive queued hints.
template<class Registers> struct X0EventRegisterWire : X0RegisterWire<Registers> {
    using Base = X0RegisterWire<Registers>;
    using Events = EventExecutor<typename Base::Driver::Token,1,1>;
    Events events;
    typename Events::Token target = Events::invalid();
    uint32_t calls = 0, matched = 0, skipped = 0;
    bool delivered = false;
    static void handle(void* user,const typename Events::Delivery& hint) {
        auto& self = *static_cast<X0EventRegisterWire*>(user);
        ++self.calls;
        const auto token = hint.message;
        if (hint.has_message && self.active && self.terminal && self.packet.kind == X0WireKind::I2c &&
            token.generation == self.request.generation && token.index == self.request.index && token.client == self.request.client &&
            self.driver.notice(token)) {
            self.delivered = true; ++self.matched;
        } else ++self.skipped;
    }
    bool bind(WorkDomain domain = WorkDomain::System,bool opted = false) {
        if (events.valid(target)) return false;
        target = events.add(handle,this,domain,opted);
        delivered = false; return events.valid(target);
    }
    bool unbind() {
        if (!events.remove(target)) return false;
        delivered = false; return true;
    }
    bool initialize(uint32_t hz = 48000000) {
        if (!Base::initialize(hz)) return false;
        events.initialize(); return bind();
    }
    completion_delivery_experiment::Send pump() {
        if (!this->active || !this->terminal || this->packet.kind != X0WireKind::I2c)
            return completion_delivery_experiment::Send::NotPending;
        return completion_delivery_experiment::pump(this->driver,events,target,this->request);
    }
    typename Events::Turn deliver(WorkOpportunity where = WorkOpportunity::Explicit,WorkProfile profile = WorkProfile::M,
                                  WorkContext context = WorkContext::Normal) {
        return events.turn(1,where,profile,context);
    }
    bool take(uint8_t owner,bool& ok,uint8_t* out,unsigned capacity) {
        if (this->packet.kind == X0WireKind::I2c && !delivered) return false;
        if (!Base::take(owner,ok,out,capacity)) return false;
        delivered = false; return true;
    }
};
template<unsigned Candidate,class Registers>
using EventRegisterX0 = FeatureAdapter<Candidate,WireX0Meaning<typename FeatureInbox<Candidate>::Type,X0EventRegisterWire<Registers>>>;
} // namespace experiment
