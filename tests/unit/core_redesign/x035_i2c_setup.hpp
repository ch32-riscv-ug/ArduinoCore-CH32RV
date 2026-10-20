#pragma once
#include "i2c_resources.hpp"

namespace x035_i2c_setup_experiment {
constexpr uintptr_t RccControl=0x40021000u,RccConfig=0x40021004u;
constexpr uintptr_t Apb2=0x40021018u,Apb1=0x4002101cu;
constexpr uintptr_t Remap=0x40010004u,PinConfig=0x40010804u;
constexpr uint32_t Gates2=5,Gates1=0x00200000u,RouteMask=0x1cu;
constexpr uint32_t PinMask=0x0000ff00u,PinMode=0x00009900u;

// Nominal 48 MHz HSI and HPRE decoding, not a frequency measurement. The
// pinned X035 SDK declares PCLK1/PCLK2 equal to HCLK (no APB divider here).
template<class Bus> uint32_t nominal_pclk() {
    if((Bus::read32(RccControl)&3)!=3)return 0;
    const unsigned code=(Bus::read32(RccConfig)>>4)&15;
    return code<8?48000000u/(code+1):48000000u>>(code-7);
}

// Fixed PA10/PA11 default route only, serialized caller, cold/quiescent proof.
// No interrupt exclusion, actual wire/clock validation, or GPIO clock release.
template<class Bus,class Resources> class Setup {
    Resources& resources_;
    typename Resources::Ticket ticket_{};
    bool held_=false,prepared_=false;
    bool gates() const {
        return (Bus::read32(Apb2)&Gates2)==Gates2 && (Bus::read32(Apb1)&Gates1)==Gates1;
    }
    bool idle() const {
        using namespace i2c_experiment;
        // Do not read STAR2 while a live local controller may be using it.
        return !(Bus::read16(Base+C1)&(PE|START|STOP|0x8000)) &&
               !(Bus::read16(Base+C2)&0x1f00) && !(Bus::read16(Base+S2)&3);
    }
    bool matches() const {
        const auto hz=nominal_pclk<Bus>();
        return hz && hz==resources_.frequency() && gates() && idle() &&
               !(Bus::read32(Remap)&RouteMask) && (Bus::read32(PinConfig)&PinMask)==PinMode;
    }
public:
    enum class Result { Ready,Busy,ResourceBusy,ClockMismatch,FaultHeld,Precondition };
    constexpr explicit Setup(Resources& resources):resources_(resources){}
    Setup(const Setup&)=delete;Setup& operator=(const Setup&)=delete;
    Result prepare(bool cold_quiescent) {
        if(held_)return Result::Busy;
        if(!cold_quiescent)return Result::Precondition;
        if(!resources_.claim(this,7,ticket_))return Result::ResourceBusy;
        held_=true;prepared_=false;
        if(nominal_pclk<Bus>()!=resources_.frequency()) {
            resources_.release(this,ticket_);held_=false;return Result::ClockMismatch;
        }
        Bus::write32(Apb2,Bus::read32(Apb2)|Gates2);
        Bus::write32(Apb1,Bus::read32(Apb1)|Gates1);
        if(!gates()||!idle())return Result::FaultHeld;
        Bus::write32(Remap,Bus::read32(Remap)&~RouteMask);
        Bus::write32(PinConfig,(Bus::read32(PinConfig)&~PinMask)|PinMode);
        if(!matches())return Result::FaultHeld;
        prepared_=true;
        if(!resources_.release(this,ticket_))return Result::FaultHeld;
        held_=false;return Result::Ready;
    }
    // External adapter's stop witness. No reset/stop is performed here.
    bool abandon(bool quiescent) {
        if(!held_||!quiescent)return false;
        if(!resources_.release(this,ticket_))return false;
        held_=prepared_=false;return true;
    }
    bool ready() const{return prepared_&&!held_&&matches();}
    bool held() const{return held_;}
    bool prepared() const{return prepared_;}
};
} // namespace x035_i2c_setup_experiment
