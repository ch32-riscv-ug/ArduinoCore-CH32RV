#pragma once
#include "k0_shared.hpp"

namespace experiment {
// Conditional N03 register adapter. Admission=0 ACKs and discards a rejected
// write; Admission=1 NACKs its data and needs explicit rearm after bus idle.
// Neither is an adopted public wire protocol. START to a foreign address is
// not observable here: the fixture must restrict repeated addressing to us.
template<class Application,class Registers,unsigned Admission,unsigned Capacity> struct K0SlaveRegister {
    static_assert(Admission < 2,"two experimental admission mappings");
    static_assert(Capacity==1 || Capacity==2,"fixed K0 capacity");
    static constexpr uint16_t Addr=2,Btf=4,Stop=16,Rx=64,Tx=128;
    static constexpr uint16_t Berr=256,Arlo=512,Af=1024,Ovr=2048;
    static constexpr uint16_t Pe=1,Ack=1024,Busy=2,Tra=4;
    enum class Mode : uint8_t { Idle, Receive, Transmit, Blocked, Fault };
    Mode mode=Mode::Idle;

    void drain(Application& app,Registers& io) {
        // Bound each entry. A delayed receiver may have DR + shift data.
        for (unsigned i=0;i<2;++i) {
            if (!(io.read_star1() & Rx)) break;
            const uint8_t byte=uint8_t(io.read_data());
            if (mode==Mode::Receive) app.byte(byte);
        }
    }
    void service(Application& app,Registers& io) {
        uint16_t flags=io.read_star1();
        if (mode==Mode::Fault) return;
        if (flags & (Berr|Arlo|Ovr)) {
            // This experiment fails closed. No partial frame is committed.
            app.restart(); mode=Mode::Fault;
            io.write_control(uint16_t(io.read_control() & ~(Pe|Ack)));
            io.clear_errors(uint16_t(flags & (Berr|Arlo|Ovr|Af)));
            return;
        }
        drain(app,io); // Includes the final data before STOP / a new ADDR.
        flags=io.read_star1();
        if (flags & Stop) {
            app.stop(); mode=Mode::Idle;
            // read_star1 above, then CTLR1 write: preserve configuration.
            io.write_control(uint16_t(io.read_control() | Ack));
        }
        if (flags & Af) {
            io.clear_errors(Af);
            if (mode==Mode::Transmit) { app.stop(); mode=Mode::Idle; }
        }
        if (flags & Addr) {
            typename Application::Scope scope;
            // Snapshot before the SR2 read releases address stretching. The
            // direction read has a side effect; avoid SDK get-event helpers.
            // An unused snapshot is also taken for receive addresses.
            app.begin_read();
            if constexpr (Admission==1) {
                // Direction is not known until SR2 releases stretching. Set
                // the tentative receiver ACK policy first; restore for TX.
                if (app.inbox.size()>=Capacity)
                    io.write_control(uint16_t(io.read_control() & ~Ack));
            }
            const uint16_t direction=io.read_star2();
            if (direction & Tra) {
                mode=Mode::Transmit;
                if constexpr (Admission==1)
                    io.write_control(uint16_t(io.read_control() | Ack));
            }
            else {
                app.begin_write();
                mode=app.accepted ? Mode::Receive : Mode::Blocked;
            }
        }
        flags=io.read_star1();
        if (mode==Mode::Transmit && (flags & Tx)) {
            app.read(); io.write_data(uint8_t(app.reply));
        }
    }
    bool rearm(Application& app,Registers& io) {
        // Only the NACK branch disables ACK; no address is assumed accepted
        // while ACK=0. This assumption and idle recovery need physical proof.
        typename Application::Scope scope;
        (void)app;
        if constexpr (Admission==0) return false;
        if (mode!=Mode::Blocked || (io.read_star2() & Busy)) return false;
        io.write_control(uint16_t(io.read_control() | Ack));
        mode=Mode::Idle; return true;
    }
    bool recover(Application& app,Registers& io) {
        typename Application::Scope scope;
        if (mode!=Mode::Fault || (io.read_star2() & Busy)) return false;
        app.restart();
        // Peripheral/pin/clock preparation is external. No reset side effects
        // beyond PE clear are inferred from this recovery experiment.
        io.write_control(uint16_t(io.read_control() | Pe | Ack));
        mode=Mode::Idle; return true;
    }
};
} // namespace experiment
