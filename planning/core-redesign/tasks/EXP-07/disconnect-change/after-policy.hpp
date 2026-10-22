#pragma once
#include "../../../../../tests/unit/core_redesign/connection_types.hpp"
#include <limits>
namespace disconnect_policy {
struct Policy {
    struct Message {connection_experiment::Command command;uint32_t session;};
    uint32_t session=0,active_session=0;
    connection_experiment::Attach attach(){
        if(session==UINT32_MAX)return connection_experiment::Attach::Exhausted;
        ++session;return connection_experiment::Attach::Accepted;
    }
    uint32_t epoch() const{return session;}
    Message pack(int16_t kind) const{return {{kind,0},session};}
    bool current(const Message& message) const{return message.session==session;}
    void request_started(){active_session=session;}
    uint32_t request_epoch() const{return active_session;}
    bool result_current() const{return active_session==session;}
};
static_assert(sizeof(Policy::Message)==8,"recovery tags each queued input");
}
