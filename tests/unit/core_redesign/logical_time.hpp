#pragma once
#include <stdint.h>
#include <limits>
#include <type_traits>
#include "deadline_timer.hpp"

// Pure arithmetic experiment. Samples, their stable source epoch/rate, and gap
// bounds are declarations from the caller, not measurements made by this class.
// No MMIO, IRQ, automatic tick, callback, sleep or scheduler is provided.
namespace time_experiment {
struct Sample { uint64_t raw;uint32_t epoch,hz; };
struct Gap { uint64_t upper;bool known; };
enum class Quality : uint8_t { Declared=0,Assumed=1,Rounded=2 };
struct Stamp { uint64_t us;uint32_t epoch;uint8_t quality; };
struct Dropped { uint32_t numerator,denominator; };
enum class Result : uint8_t {
    Ready,ReadyAssumed,Busy,Closed,Paused,Invalid,StaleSource,ChangedRate,
    UnknownGap,AmbiguousGap,BoundViolation,Backward,Overflow,Exhausted,PrecisionLoss
};
enum class State : uint8_t { Empty,Running,Paused,Stopped };
inline bool ready(Result r){return r==Result::Ready||r==Result::ReadyAssumed;}

struct VariableRate {
    using Fraction=uint32_t;
    uint32_t hz=0;
    static bool accepts(uint32_t rate){return rate!=0;}
    void set_rate(uint32_t rate){hz=rate;}
    uint32_t rate() const{return hz;}
    uint32_t denominator() const{return hz;}
    bool elapsed(uint32_t delta,Fraction fraction,uint64_t& us,Fraction& next) const{
        const uint64_t numerator=uint64_t(delta)*1000000u+fraction;
        us=numerator/hz;next=uint32_t(numerator-us*hz);return true;
    }
    bool elapsed(uint64_t delta,Fraction fraction,uint64_t& us,Fraction& next) const{
        const uint64_t seconds=delta/hz,remainder=delta-seconds*hz;
        if(seconds>UINT64_MAX/1000000u)return false;
        const uint64_t numerator=remainder*1000000u+fraction;
        const uint64_t small=numerator/hz,large=seconds*1000000u;
        if(large>UINT64_MAX-small)return false;
        us=large+small;next=uint32_t(numerator-small*hz);return true;
    }
};
struct SixMHz {
    using Fraction=uint8_t;
    static bool accepts(uint32_t rate){return rate==6000000u;}
    void set_rate(uint32_t){}
    static uint32_t rate(){return 6000000u;}
    static uint32_t denominator(){return 6;}
    static bool elapsed(uint32_t delta,Fraction fraction,uint64_t& us,Fraction& next){
        uint32_t whole=delta/6;unsigned remainder=delta-whole*6+fraction;
        if(remainder>=6){++whole;remainder-=6;}
        us=whole;next=uint8_t(remainder);return true;
    }
    static bool elapsed(uint64_t delta,Fraction fraction,uint64_t& us,Fraction& next){
        uint64_t whole=delta/6;unsigned remainder=unsigned(delta-whole*6)+fraction;
        if(remainder>=6){++whole;remainder-=6;}
        us=whole;next=uint8_t(remainder);return true;
    }
};

template<unsigned Bits,bool AllowAssumed=false,class Conversion=VariableRate,class Epoch=uint32_t>
class LogicalClock : private Conversion {
    static_assert(Bits==32||Bits==64,"source counter width");
    static_assert(std::is_unsigned<Epoch>::value&&!std::is_same<Epoch,bool>::value,"unsigned epoch");
    using Raw=typename std::conditional<Bits==32,uint32_t,uint64_t>::type;
    Raw last_raw=0;
    uint64_t elapsed_us=0;
    uint32_t source_epoch=0;
    Epoch domain_epoch=0;
    typename Conversion::Fraction fraction=0;
    State state=State::Empty;
    uint8_t quality=0;
    static bool valid(const Sample& s){return s.epoch&&Conversion::accepts(s.hz)&&(Bits==64||s.raw<=0xffffffffu);}
    void stamp(Stamp& out) const{out.us=elapsed_us;out.epoch=domain_epoch;out.quality=quality;}
    Result good() const{return quality&uint8_t(Quality::Assumed)?Result::ReadyAssumed:Result::Ready;}
public:
    LogicalClock()=default;LogicalClock(const LogicalClock&)=delete;LogicalClock& operator=(const LogicalClock&)=delete;
    State phase() const{return state;}
    uint64_t history_us() const{return elapsed_us;}
    uint64_t history_raw() const{return last_raw;}
    uint32_t epoch() const{return domain_epoch;}
    uint32_t source() const{return source_epoch;}
    uint32_t frequency() const{return Conversion::rate();}
    uint32_t remainder() const{return fraction;}
    uint32_t remainder_denominator() const{return Conversion::denominator();}
    uint8_t history_quality() const{return quality;}
    Result begin(const Sample& sample,uint64_t anchor,Stamp& out){
        if(state==State::Running||state==State::Paused)return Result::Busy;
        if(!valid(sample))return Result::Invalid;
        if(domain_epoch==std::numeric_limits<Epoch>::max())return Result::Exhausted;
        ++domain_epoch;source_epoch=sample.epoch;last_raw=Raw(sample.raw);elapsed_us=anchor;
        fraction=0;quality=0;Conversion::set_rate(sample.hz);state=State::Running;stamp(out);return Result::Ready;
    }
    Result observe(const Sample& sample,Gap gap,Stamp& out){
        if(state==State::Paused)return Result::Paused;
        if(state!=State::Running)return Result::Closed;
        if(!valid(sample))return Result::Invalid;
        if(sample.epoch!=source_epoch)return Result::StaleSource;
        if(sample.hz!=Conversion::rate())return Result::ChangedRate;
        uint64_t delta=0;bool assumed=false;
        if constexpr(Bits==32){
            delta=uint32_t(uint32_t(sample.raw)-last_raw);
            if(gap.known){
                if(gap.upper>=0x100000000ull)return Result::AmbiguousGap;
                if(delta>gap.upper)return Result::BoundViolation;
            }else if constexpr(AllowAssumed)assumed=true;
            else return Result::UnknownGap;
        }else{
            if(sample.raw<last_raw)return Result::Backward;
            delta=sample.raw-last_raw;
            if(gap.known&&delta>gap.upper)return Result::BoundViolation;
        }
        uint64_t add=0;typename Conversion::Fraction next=0;
        if(!Conversion::elapsed(Raw(delta),fraction,add,next)||elapsed_us>UINT64_MAX-add)return Result::Overflow;
        elapsed_us+=add;fraction=next;last_raw=Raw(sample.raw);
        if(assumed)quality|=uint8_t(Quality::Assumed);
        stamp(out);return good();
    }
    bool pause(){if(state!=State::Running)return false;state=State::Paused;return true;}
    bool stop(){if(state==State::Empty||state==State::Stopped)return false;state=State::Stopped;return true;}
    Result resume_known(const Sample& sample,uint64_t gap_us,bool allow_drop,Stamp& out,Dropped& drop){
        if(state!=State::Paused)return Result::Closed;
        if(!valid(sample))return Result::Invalid;
        const bool loses=sample.hz!=Conversion::rate()&&fraction;
        if(loses&&!allow_drop)return Result::PrecisionLoss;
        if(elapsed_us>UINT64_MAX-gap_us)return Result::Overflow;
        drop.numerator=loses?fraction:0;drop.denominator=Conversion::denominator();
        if(sample.hz!=Conversion::rate())fraction=0;
        if(loses)quality|=uint8_t(Quality::Rounded);
        elapsed_us+=gap_us;source_epoch=sample.epoch;last_raw=Raw(sample.raw);Conversion::set_rate(sample.hz);
        state=State::Running;stamp(out);return good();
    }
    Result resume_unknown(const Sample& sample,uint64_t anchor,Stamp& out){
        if(state!=State::Paused)return Result::Closed;
        if(!valid(sample))return Result::Invalid;
        if(domain_epoch==std::numeric_limits<Epoch>::max())return Result::Exhausted;
        ++domain_epoch;source_epoch=sample.epoch;last_raw=Raw(sample.raw);elapsed_us=anchor;
        fraction=0;quality=0;Conversion::set_rate(sample.hz);state=State::Running;stamp(out);return Result::Ready;
    }
};

enum class TimerPoll : uint8_t { Updated,Unchanged,NoSource,Untrusted,Unarmed,DomainChanged,Backward,GapTooLarge };
struct Notice { experiment::DeadlineTimer::Expiration expiration;uint32_t epoch=0; };
// This adapter checks the existing 32-bit timer's observation half-range using
// the accepted 64-bit stamp. It does not make the timer a 64-bit deadline timer.
template<bool AllowAssumed=false,bool AllowRounded=false> class DomainTimer {
    experiment::DeadlineTimer timer;
    uint64_t last_us=0;
    uint32_t domain=0;
    bool bound=false;
    static bool trusted(const Stamp& stamp){
        return (AllowAssumed||!(stamp.quality&uint8_t(Quality::Assumed)))&&
               (AllowRounded||!(stamp.quality&uint8_t(Quality::Rounded)));
    }
public:
    bool arm(Result result,const Stamp& stamp,uint32_t delay,uint32_t period){
        if(!ready(result)||!stamp.epoch||!trusted(stamp))return false;
        if(!timer.arm(uint32_t(stamp.us),delay,period))return false;
        last_us=stamp.us;domain=stamp.epoch;bound=true;return true;
    }
    TimerPoll poll(Result result,const Stamp& stamp){
        if(!ready(result))return TimerPoll::NoSource;
        if(!bound)return TimerPoll::Unarmed;
        if(stamp.epoch!=domain){timer.stop();bound=false;return TimerPoll::DomainChanged;}
        if(!trusted(stamp))return TimerPoll::Untrusted;
        if(stamp.us<last_us){timer.stop();bound=false;return TimerPoll::Backward;}
        if(stamp.us-last_us>=experiment::DeadlineTimer::half){timer.stop();bound=false;return TimerPoll::GapTooLarge;}
        last_us=stamp.us;return timer.update(uint32_t(stamp.us))?TimerPoll::Updated:TimerPoll::Unchanged;
    }
    bool take(Notice& out){
        experiment::DeadlineTimer::Expiration expiration;
        if(!timer.take(expiration))return false;
        out.expiration=expiration;out.epoch=domain;return true;
    }
    bool armed() const{return timer.armed();}
    bool pending() const{return timer.ready();}
    uint32_t epoch() const{return domain;}
};
} // namespace time_experiment
