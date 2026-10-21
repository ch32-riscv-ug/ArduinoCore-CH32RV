#pragma once
#include "x035_i2c_setup.hpp"
namespace x035_i2c_route_experiment {
using x035_i2c_setup_experiment::nominal_pclk;
using x035_i2c_setup_experiment::Apb1;
using x035_i2c_setup_experiment::Apb2;
using x035_i2c_setup_experiment::Remap;
using x035_i2c_setup_experiment::RouteMask;
using x035_i2c_setup_experiment::Gates1;
enum class Package { C8, F7 };
// Compile-time register mapping, not a conflict detector for USB/debug users.
template<unsigned Route> struct Mapping {
    static_assert(Route<8,"three-bit route");
    static constexpr uintptr_t pin_config=Route<2?0x40010804u:0x4001101cu;
    static constexpr uint32_t gates=Route<2?5:17;
    static constexpr unsigned shift=Route==1?20:((Route==2||Route==4||Route==6)?0:8);
    static constexpr uint32_t mask=0xffu<<shift,mode=0x99u<<shift;
    static constexpr bool debug_pins=Route==3 || Route==5 || Route==7;
};
template<class Bus,class Resources,unsigned Route,Package Device> class Setup {
    static_assert(Device==Package::C8 || Device==Package::F7,"known package required");
    using Map=Mapping<Route>;
    static constexpr uintptr_t PinConfig=Map::pin_config;
    static constexpr uint32_t Gates2=Map::gates,PinMask=Map::mask,PinMode=Map::mode,RouteBits=Route<<2;
    static constexpr bool Available=Device==Package::C8 || Route>=2;
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
        if(!hz || hz!=resources_.frequency() || !gates() || !idle())return false;
        const auto remap=Bus::read32(Remap);
        if constexpr (Map::debug_pins)if((remap&0x07000000u)!=0x04000000u)return false;
        return (remap&RouteMask)==RouteBits && (Bus::read32(PinConfig)&PinMask)==PinMode;
    }
public:
    enum class Result { Ready,Busy,ResourceBusy,ClockMismatch,FaultHeld,Precondition,UnsupportedPackage };
    constexpr explicit Setup(Resources& resources):resources_(resources){}
    Setup(const Setup&)=delete;Setup& operator=(const Setup&)=delete;
    Result prepare(bool cold_quiescent) {
        if constexpr (!Available)return Result::UnsupportedPackage;
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
        // Do not disable the debugger. Its owner must have released these pins.
        if constexpr (Map::debug_pins)
            if((Bus::read32(Remap)&0x07000000u)!=0x04000000u)return Result::FaultHeld;
        Bus::write32(Remap,(Bus::read32(Remap)&~RouteMask)|RouteBits);
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
    bool ready() const{if constexpr (!Available)return false;else return prepared_&&!held_&&matches();}
    bool held() const{return held_;}
    bool prepared() const{return prepared_;}
};
} // namespace x035_i2c_route_experiment
