#pragma once
#include "event_executor.hpp"

namespace completion_delivery_experiment {
enum class Send { Accepted, Full, Stale, NotPending };
// Experimental serial glue, shared by host and target probes. The source
// remains pending after enqueue so target removal cannot erase the only hint.
template<class Bus,class Events>
Send pump(Bus& bus,Events& events,typename Events::Token target,typename Bus::Token request) {
    if(!bus.notice(request))return Send::NotPending;
    switch(events.send(target,request)) {
    case experiment::EventSend::Accepted:return Send::Accepted;
    case experiment::EventSend::Full:return Send::Full;
    case experiment::EventSend::Stale:return Send::Stale;
    }
    return Send::Stale;
}
// A queued token is only a hint. Revalidate after end, explicit collection or
// record reuse. Rejected hints leave the caller's result object unchanged.
template<class Bus>
bool collect(Bus& bus,typename Bus::Token request,typename Bus::Completion& value) {
    return bus.notice(request)&&bus.take(request,value);
}
} // namespace completion_delivery_experiment
