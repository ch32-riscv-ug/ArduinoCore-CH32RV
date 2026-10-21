#pragma once
#include "x0_event_wire.hpp"
#include "x035_i2c_routes.hpp"

namespace experiment {
// Isolated N02 composition. Existing boolean/direct/event adapters stay frozen.
// Reuse their stop, loan, notification and collection code; only admission uses
// the real experimental preparation predicate, under the Manager's lease.
template<class Registers,unsigned Route,x035_i2c_route_experiment::Package Device,bool WithEvents>
struct X0PreparedWire : std::conditional<WithEvents,X0EventRegisterWire<Registers>,X0RegisterWire<Registers>>::type {
    using Base = typename std::conditional<WithEvents,X0EventRegisterWire<Registers>,X0RegisterWire<Registers>>::type;
    using Driver = typename Base::Driver;
    using Preparation = x035_i2c_route_experiment::Setup<Registers,typename Base::Resources,Route,Device>;
    Preparation preparation{this->resources};
    // Same acceptance/copy contract as X0RegisterWire. The inherited boolean
    // pins field is deliberately unused, retained in the measured baseline.
    bool start(uint8_t target,X0WireKind kind,uint8_t address,const uint8_t* tx,unsigned tx_size,unsigned rx_size) {
        if (!this->initialized || !target || target > 3 || tx_size > 33 || rx_size > 8 ||
            (!tx_size && !rx_size) || (tx_size && !tx)) return false;
        if (kind == X0WireKind::I2c) { if (address < 8 || address >= 0x78) return false; }
        else if (kind != X0WireKind::VirtualTouch || target != 3 || address || tx_size != 8 || rx_size) return false;
        if (this->active) { ++this->busy; return false; }
        if (kind == X0WireKind::I2c) {
            if (this->driver.open_backend(this->resources,preparation,400000) != Driver::Open::Ready) return false;
            for (unsigned i = 0; i < tx_size; ++i) if (!this->storage.write(i,tx[i])) {
                this->driver.close_backend(); this->fault = true; return false;
            }
            if (this->driver.submit(target-1,&this->storage,tx_size,rx_size,address,this->request) != i2c_experiment::Start::Accepted) {
                if (!this->driver.close_backend()) this->fault = true;
                return false;
            }
        }
        this->packet.owner = this->owner = target; this->packet.address = address; this->packet.kind = kind;
        this->packet.tx_size = uint8_t(tx_size); this->packet.rx_size = uint8_t(rx_size);
        for (unsigned i = 0; i < tx_size; ++i) this->packet.tx[i] = tx[i];
        this->virtual_success = true; this->active = true; this->terminal = false; this->due = this->now+2;
        ++this->started; ++this->owner_starts[target]; return true;
    }
};
template<unsigned Candidate,class Registers,unsigned Route,x035_i2c_route_experiment::Package Device,bool Events>
using PreparedX0 = FeatureAdapter<Candidate,WireX0Meaning<typename FeatureInbox<Candidate>::Type,X0PreparedWire<Registers,Route,Device,Events>>>;
} // namespace experiment
