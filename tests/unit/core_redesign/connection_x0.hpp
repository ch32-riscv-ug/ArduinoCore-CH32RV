#pragma once
#include "x0_features.hpp"
#include "connection_pps.hpp"
namespace experiment {
// Frozen FixedX0Meaning projection: only PPS type/template parameters changed.
template<class Inbox,class Policy,bool Touch> struct ConnectionMeaning : TouchStorage<Touch> {
    connection_experiment::Pps<Inbox,Policy> pps;
    p2::Screen screen;
    FeatureBus<Touch ? 3 : 2> bus;
    uint32_t now = 0, next_measure = 0;
    uint32_t measurement_done = 0, missed_measure = 0;
    uint8_t last_granted = 0;
    bool measure_pending = false;
    int32_t source_mv = 5000, source_ma = 1000;
    uint8_t measurement_data[8]{};

    static constexpr bool touch_feature = Touch;
    uint32_t touch_count() const { if constexpr (Touch) return this->touch_done; else return 0; }
    uint32_t touch_missed() const { if constexpr (Touch) return this->missed_touch; else return 0; }
    uint32_t touch_starts() const { if constexpr (Touch) return bus.owner_starts[3]; else return 0; }
    bool touch_due() const { if constexpr (Touch) return now >= this->next_touch; else return false; }
    bool touch_waiting() const { if constexpr (Touch) return this->touch_pending; else return false; }

    static void encode(uint8_t* out, int32_t value) {
        for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(uint32_t(value) >> (8 * i));
    }
    static int32_t decode(const uint8_t* in) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(in[i]) << (8 * i);
        return value <= 0x7fffffffu ? int32_t(value) : int32_t(-1 - int32_t(~value));
    }
    static bool same_view(const p2::View& a, const p2::View& b) {
        return a.desired == b.desired && a.confirmed == b.confirmed && a.mv == b.mv && a.ma == b.ma &&
               a.valid == b.valid && a.stale == b.stale && a.connected == b.connected;
    }
    static void periodic(uint32_t now, uint32_t period, uint32_t& next, bool& pending, uint32_t& missed) {
        if (now < next) return;
        const uint32_t count = (now - next) / period + 1;
        missed += count - 1 + (pending ? 1 : 0);
        pending = true; next += count * period;
    }
    void hardware(uint32_t time) {
        now = time; pps.hardware(time); bus.hardware(time);
    }
    bool needs_step() const {
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                            pps.valid, pps.stale, pps.connected};
        if (pps.inbox.size() || (pps.active && pps.terminal) ||
            (!pps.active && pps.selected_pending && pps.connected) || bus.terminal ||
            !same_view(view, screen.latest) || now >= next_measure ||
            touch_due() || (screen.active && screen.terminal)) return true;
        if (!screen.frame && screen.revision != screen.displayed_revision && now >= screen.next_frame) return true;
        return !bus.active && ((screen.frame && !screen.active) || measure_pending || touch_waiting());
    }
    void step() {
        bool ok;
        if (bus.take(1, ok)) screen.finish(ok);
        if (bus.take(2, ok)) {
            ++measurement_done;
            pps.measurement(ok, decode(measurement_data), decode(measurement_data + 4));
        }
        if constexpr (Touch) { if (bus.take(3, ok) && ok) ++this->touch_done; }
        pps.step();
        const p2::View view{pps.desired, pps.confirmed, pps.measured_mv, pps.measured_ma,
                        pps.valid, pps.stale, pps.connected};
        if (!same_view(view, screen.latest)) screen.update(view);
        periodic(now, 100, next_measure, measure_pending, missed_measure);
        if constexpr (Touch) periodic(now, 20, this->next_touch, this->touch_pending, this->missed_touch);
        screen.now = now;
        screen.step(false);
        if (bus.active) return;
        // Every successful transaction releases the bus before this selection.
        // No request is enqueued; readiness remains in its owning state machine.
        constexpr unsigned owners = Touch ? 3 : 2;
        for (unsigned i = 1; i <= owners; ++i) {
            const uint8_t owner = uint8_t((last_granted + i - 1) % owners + 1);
            if (owner == 1 && screen.frame && !screen.active) {
                screen.prepare_chunk();
                if (bus.try_start(1, screen.tx, 32)) { screen.mark_started(); last_granted = 1; }
                return;
            }
            if (owner == 2 && measure_pending) {
                encode(measurement_data, source_mv); encode(measurement_data + 4, source_ma);
                if (bus.try_start(2, measurement_data, 8)) { measure_pending = false; last_granted = 2; }
                return;
            }
            if constexpr (Touch) {
                if (owner == 3 && this->touch_pending) {
                    encode(this->touch_data, int32_t(this->touch_done + 1)); encode(this->touch_data + 4, 0);
                    if (bus.try_start(3, this->touch_data, 8)) { this->touch_pending = false; last_granted = 3; }
                    return;
                }
            }
        }
    }
};


template<class Message> struct ConnectionDirectInbox {
    Message messages[4]{};uint8_t head=0,count=0;
    unsigned size() const{return count;}
    bool push(const Message& value){if(count==4)return false;messages[(head+count)%4]=value;++count;return true;}
    bool pop(Message& value){if(!count)return false;value=messages[head];head=uint8_t((head+1)%4);--count;return true;}
};
template<class Message> struct ConnectionBatchInbox {
    using Mailbox=StaticMailbox<Message,4>;Mailbox mailbox;
    const typename Mailbox::Batch* current=nullptr;unsigned cursor=0;
    unsigned size() const{return current?current->count-cursor:mailbox.size();}
    bool push(const Message& value){return mailbox.send(value)==EventSend::Accepted;}
    bool pop(Message& value){if(!current || cursor==current->count)return false;value=current->messages[cursor++];return true;}
};
template<unsigned C,class Message> struct ConnectionInbox {using Type=Queue<Message,4>;};
template<class Message> struct ConnectionInbox<1,Message>{using Type=ConnectionDirectInbox<Message>;};
template<class Message> struct ConnectionInbox<4,Message>{using Type=ConnectionBatchInbox<Message>;};
template<unsigned C,class Policy,bool Touch> struct ConnectionX0
 :FeatureAdapter<C,ConnectionMeaning<typename ConnectionInbox<C,typename Policy::Message>::Type,Policy,Touch>> {};
template<class Policy,bool Touch> struct ConnectionX0<4,Policy,Touch>
 :ConnectionMeaning<ConnectionBatchInbox<typename Policy::Message>,Policy,Touch> {
    void begin(WorkDomain domain=WorkDomain::System,bool opt=false){this->pps.inbox.mailbox.begin(domain,opt);changed();}
    void changed(){if(this->needs_step())this->pps.inbox.mailbox.notify(1);}
    TurnStatus opportunity(WorkOpportunity where,WorkProfile profile,WorkContext context=WorkContext::Normal){
        using Batch=typename ConnectionBatchInbox<typename Policy::Message>::Mailbox::Batch;
        return this->pps.inbox.mailbox.deliver(where,profile,context,[&](const Batch& batch){
            this->pps.inbox.current=&batch;this->pps.inbox.cursor=0;this->step();this->pps.inbox.current=nullptr;changed();
        });
    }
};
}
