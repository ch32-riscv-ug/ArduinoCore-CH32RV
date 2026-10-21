#pragma once
#include "x0_features.hpp"
#include "display_parts.hpp"

namespace experiment {
// Experimental transport boundary. TX is copied on acceptance; RX belongs to
// the bus until take. No const input buffer is used as a receive destination.
// The third owner is explicitly virtual: no touch-controller protocol is known.
enum class X0WireKind : uint8_t { I2c, VirtualTouch };
struct X0WirePacket {
    uint8_t owner = 0, address = 0, tx_size = 0, rx_size = 0;
    X0WireKind kind = X0WireKind::I2c;
    uint8_t tx[33]{};
};
struct X0WireFake {
    X0WirePacket packet;
    uint8_t rx[8]{};
    uint8_t owner = 0;
    uint16_t source_bus_word = 10000, source_current_word = 1000;
    uint32_t now = 0, due = 0, sealed_hash = 0;
    uint32_t started = 0, completed = 0, reclaimed = 0, busy = 0, corruptions = 0;
    uint32_t owner_starts[4]{};
    bool active = false, terminal = false, success = false, fail_next[4]{};
    bool start(uint8_t target, X0WireKind kind, uint8_t address,
               const uint8_t* tx, unsigned tx_size, unsigned rx_size) {
        if (!target || target > 3 || tx_size > sizeof(packet.tx) || rx_size > sizeof(rx) ||
            (!tx_size && !rx_size) || (tx_size && !tx)) return false;
        if (kind == X0WireKind::I2c) { if (address > 127) return false; }
        else if (kind != X0WireKind::VirtualTouch || target != 3 || address || tx_size != 8 || rx_size) return false;
        if (active) { ++busy; return false; }
        packet.owner = owner = target; packet.address = address; packet.kind = kind;
        packet.tx_size = uint8_t(tx_size); packet.rx_size = uint8_t(rx_size);
        for (unsigned i = 0; i < tx_size; ++i) packet.tx[i] = tx[i];
        for (auto& byte : rx) byte = 0;
        sealed_hash = p2::bytes_hash(packet.tx, tx_size);
        active = true; terminal = false; success = !fail_next[target]; fail_next[target] = false;
        due = now+2; ++started; ++owner_starts[target]; return true;
    }
    void hardware(uint32_t time) {
        now = time;
        if (active && p2::bytes_hash(packet.tx, packet.tx_size) != sealed_hash) ++corruptions;
        if (!active || terminal || now < due) return;
        // Raw values are sampled at this synthetic completion, not at submit.
        // Two registers are independent reads, not a coherent/fresh ADC pair.
        if (success && packet.kind == X0WireKind::I2c && packet.address == 0x40 &&
            packet.tx_size == 1 && packet.rx_size == 2) {
            const uint16_t word = packet.tx[0] == 2 ? source_bus_word : source_current_word;
            rx[0] = uint8_t(word >> 8); rx[1] = uint8_t(word);
        }
        terminal = true; ++completed;
    }
    bool take(uint8_t target, bool& ok, uint8_t* out, unsigned capacity) {
        if (!active || !terminal || owner != target || capacity < packet.rx_size || (packet.rx_size && !out)) return false;
        ok = success;
        if (ok) for (unsigned i = 0; i < packet.rx_size; ++i) out[i] = rx[i];
        active = terminal = false; owner = 0; ++reclaimed; return true;
    }
    bool cancel() {
        if (!active || terminal) return false;
        success = false; terminal = true; ++completed; return true;
    }
};

template<class Inbox, class Bus = X0WireFake> struct WireX0Meaning : TouchStorage<true> {
    X0Pps<Inbox> pps;
    display_experiment::Screen<display_experiment::Mono64<32>> screen;
    Bus bus;
    enum DisplayPhase : uint8_t { DisplayIdle, CommandActive, NeedData, DataActive };
    enum MeasurePhase : uint8_t { MeasureIdle, BusActive, NeedCurrent, CurrentActive };
    DisplayPhase display_phase = DisplayIdle;
    MeasurePhase measure_phase = MeasureIdle;
    uint32_t now = 0, next_measure = 0, measurement_done = 0, missed_measure = 0;
    uint16_t bus_word = 0, current_word = 0;
    uint8_t measurement_data[8]{};
    uint8_t last_granted = 0;
    bool measure_pending = false;
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
        return !bus.active && ((screen.frame && (!screen.active || display_phase == NeedData)) ||
            measure_phase == NeedCurrent || (measure_phase == MeasureIdle && measure_pending) || this->touch_pending);
    }
    void measurement_finish(bool ok) {
        ++measurement_done;
        if (ok) {
            const int32_t mv = int32_t(bus_word >> 3)*4;
            const int32_t ma = current_word < 32768 ? int32_t(current_word) : int32_t(current_word)-65536;
            Helpers::encode(measurement_data,mv); Helpers::encode(measurement_data+4,ma);
            pps.measurement(true,mv,ma);
        } else pps.measurement(false,0,0);
        measure_phase = MeasureIdle;
    }
    void step() {
        bool ok; uint8_t received[2]{};
        if (bus.take(1,ok,nullptr,0)) {
            if (ok && display_phase == CommandActive) display_phase = NeedData;
            else { screen.finish(ok); display_phase = DisplayIdle; }
        }
        if (bus.take(2,ok,received,sizeof(received))) {
            if (!ok) measurement_finish(false);
            else {
                const uint16_t word = uint16_t((uint16_t(received[0])<<8)|received[1]);
                if (measure_phase == BusActive) { bus_word = word; measure_phase = NeedCurrent; }
                else { current_word = word; measurement_finish(true); }
            }
        }
        if (bus.take(3,ok,nullptr,0) && ok) ++this->touch_done;
        pps.step();
        const auto current = view();
        if (!display_experiment::same(current,screen.latest)) screen.update(current);
        Helpers::periodic(now,100,next_measure,measure_pending,missed_measure);
        Helpers::periodic(now,20,this->next_touch,this->touch_pending,this->missed_touch);
        screen.now = now; screen.step();
        if (bus.active) return;
        for (unsigned n = 1; n <= 3; ++n) {
            const uint8_t owner = uint8_t((last_granted+n-1)%3+1);
            if (owner == 1 && screen.frame && (!screen.active || display_phase == NeedData)) {
                if (display_phase == NeedData) {
                    uint8_t packet[33]; packet[0] = 0x40;
                    for (unsigned i = 0; i < screen.chunk_size(); ++i) packet[i+1] = screen.tx[i];
                    if (bus.start(1,X0WireKind::I2c,0x3c,packet,screen.chunk_size()+1,0)) display_phase = DataActive;
                    else return;
                } else {
                    if (!screen.prepare_chunk()) return;
                    const uint8_t column = uint8_t(screen.offset%128), page = uint8_t(screen.offset/128);
                    const uint8_t command[9] = {0,0x20,0,0x21,column,uint8_t(column+screen.chunk_size()-1),0x22,page,page};
                    if (!bus.start(1,X0WireKind::I2c,0x3c,command,sizeof(command),0)) return;
                    screen.mark_started(); display_phase = CommandActive;
                }
                last_granted = 1; return;
            }
            if (owner == 2 && (measure_phase == NeedCurrent || (measure_phase == MeasureIdle && measure_pending))) {
                const uint8_t pointer = measure_phase == NeedCurrent ? 4 : 2;
                if (bus.start(2,X0WireKind::I2c,0x40,&pointer,1,2)) {
                    if (measure_phase == MeasureIdle) { measure_pending = false; measure_phase = BusActive; }
                    else measure_phase = CurrentActive;
                    last_granted = 2;
                }
                return;
            }
            if (owner == 3 && this->touch_pending) {
                Helpers::encode(this->touch_data,int32_t(this->touch_done+1)); Helpers::encode(this->touch_data+4,0);
                if (bus.start(3,X0WireKind::VirtualTouch,0,this->touch_data,8,0)) { this->touch_pending = false; last_granted = 3; }
                return;
            }
        }
    }
};
template<unsigned Candidate>
using WireX0 = FeatureAdapter<Candidate,WireX0Meaning<typename FeatureInbox<Candidate>::Type>>;
} // namespace experiment
