#pragma once
#include "x0_features.hpp"
#include "display_parts.hpp"

namespace experiment {
// Same serial ownership as FeatureBus, but a common length-based fake wire cost.
struct DisplayBus : FeatureBus<3> {
    bool try_start(uint8_t owner, const uint8_t* data, uint8_t size) {
        if (!data || !size) return false;
        if (!FeatureBus<3>::try_start(owner, data, size)) return false;
        due = now+1+(unsigned(size)+15)/16;
        return true;
    }
};
template<class Inbox, class Panel> struct DisplayX0Meaning : TouchStorage<true> {
    X0Pps<Inbox> pps;
    display_experiment::Screen<Panel> screen;
    DisplayBus bus;
    uint32_t now = 0, next_measure = 0, measurement_done = 0, missed_measure = 0;
    uint8_t last_granted = 0;
    bool measure_pending = false;
    int32_t source_mv = 5000, source_ma = 1000;
    uint8_t measurement_data[8]{};
    using Helpers = FixedX0Meaning<Inbox, true>;
    void hardware(uint32_t time) { now = time; pps.hardware(time); bus.hardware(time); }
    display_experiment::View view() const {
        return {pps.desired,pps.confirmed,pps.measured_mv,pps.measured_ma,pps.valid,pps.stale,pps.connected};
    }
    bool needs_step() const {
        if (pps.inbox.size() || (pps.active && pps.terminal) || (!pps.active && pps.selected_pending && pps.connected) ||
            bus.terminal || !display_experiment::same(view(),screen.latest) || now >= next_measure ||
            now >= this->next_touch || (screen.active && screen.terminal)) return true;
        if (!screen.frame && screen.revision != screen.displayed_revision && now >= screen.next_frame) return true;
        return !bus.active && ((screen.frame && !screen.active) || measure_pending || this->touch_pending);
    }
    void step() {
        bool ok;
        if (bus.take(1, ok)) screen.finish(ok);
        if (bus.take(2, ok)) {
            ++measurement_done;
            pps.measurement(ok,Helpers::decode(measurement_data),Helpers::decode(measurement_data+4));
        }
        if (bus.take(3, ok) && ok) ++this->touch_done;
        pps.step();
        const auto current = view();
        if (!display_experiment::same(current,screen.latest)) screen.update(current);
        Helpers::periodic(now,100,next_measure,measure_pending,missed_measure);
        Helpers::periodic(now,20,this->next_touch,this->touch_pending,this->missed_touch);
        screen.now = now; screen.step();
        if (bus.active) return;
        for (unsigned n = 1; n <= 3; ++n) {
            const uint8_t owner = uint8_t((last_granted+n-1)%3+1);
            if (owner == 1 && screen.frame && !screen.active) {
                if (screen.prepare_chunk() && bus.try_start(1,screen.tx,uint8_t(screen.chunk_size()))) {
                    screen.mark_started(); last_granted = 1;
                }
                return;
            }
            if (owner == 2 && measure_pending) {
                Helpers::encode(measurement_data,source_mv); Helpers::encode(measurement_data+4,source_ma);
                if (bus.try_start(2,measurement_data,8)) { measure_pending = false; last_granted = 2; }
                return;
            }
            if (owner == 3 && this->touch_pending) {
                Helpers::encode(this->touch_data,int32_t(this->touch_done+1)); Helpers::encode(this->touch_data+4,0);
                if (bus.try_start(3,this->touch_data,8)) { this->touch_pending = false; last_granted = 3; }
                return;
            }
        }
    }
};
template<unsigned Candidate, class Panel>
using DisplayX0 = FeatureAdapter<Candidate, DisplayX0Meaning<typename FeatureInbox<Candidate>::Type, Panel>>;
} // namespace experiment
