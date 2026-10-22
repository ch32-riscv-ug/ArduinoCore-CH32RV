#pragma once
#include "../../../../../tests/unit/core_redesign/connection_types.hpp"
namespace disconnect_policy {
struct Policy {
    struct Message {connection_experiment::Command command;};
    bool used=false;
    connection_experiment::Attach attach(){if(used)return connection_experiment::Attach::Unsupported;used=true;return connection_experiment::Attach::Accepted;}
    uint32_t epoch() const{return used?1:0;}
    Message pack(int16_t kind) const{return {{kind,0}};}
    bool current(const Message&) const{return true;}
    void request_started(){}
    uint32_t request_epoch() const{return 1;}
    bool result_current() const{return true;}
};
static_assert(sizeof(Policy::Message)==4,"baseline has no reconnect envelope");
}
