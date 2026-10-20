#pragma once
#include <stdint.h>
namespace runtime_experiment {
constexpr uintptr_t RccControl=0x40021000u, RccConfig=0x40021004u, FlashAccess=0x40022000u;
constexpr uintptr_t PllTrim=0x1ffff7d4u, TickControl=0xe000f000u, TickStatus=0xe000f004u;
constexpr uintptr_t TickLow=0xe000f008u, TickHigh=0xe000f00cu, TickCompareLow=0xe000f010u, TickCompareHigh=0xe000f014u;
enum class ClockStage { Idle, Hsi, Pll, Verify, Complete, Abandoned };
enum class ClockBegin { Accepted, Busy, NotOwned, UnsupportedState };
// Cold-compatible exclusive clock experiment. Each step is finite. No pump,
// deadline, rollback or claim that register confirmation measures frequency.
template<class Bus,bool X035> struct Clock48 {
    ClockStage stage=ClockStage::Idle;
    Clock48()=default;
    Clock48(const Clock48&)=delete;
    Clock48& operator=(const Clock48&)=delete;
    ClockBegin begin(bool owned) {
        if(stage!=ClockStage::Idle)return ClockBegin::Busy;
        if(!owned)return ClockBegin::NotOwned;
        const auto ctl=Bus::read(RccControl);
        if constexpr (!X035) {
            if((ctl&0x03000000u)||(Bus::read(RccConfig)&15u))return ClockBegin::UnsupportedState;
        }
        Bus::write(RccControl,ctl|1u);stage=ClockStage::Hsi;return ClockBegin::Accepted;
    }
    ClockStage step() {
        if(stage==ClockStage::Hsi) {
            if(!(Bus::read(RccControl)&2u))return stage;
            Bus::write(FlashAccess,(Bus::read(FlashAccess)&~3u)|(X035?2u:1u));
            if constexpr (X035) {
                Bus::write(RccConfig,Bus::read(RccConfig)&~0xf0u);stage=ClockStage::Verify;
            } else {
                const uint32_t trim=Bus::read_byte(PllTrim);
                if(trim!=255u)Bus::write(RccControl,(Bus::read(RccControl)&~0xf8u)|((trim&31u)<<3));
                Bus::write(RccConfig,Bus::read(RccConfig)&~0x100f0u);
                Bus::write(RccControl,Bus::read(RccControl)|0x01000000u);stage=ClockStage::Pll;
            }
        } else if(stage==ClockStage::Pll) {
            if(!(Bus::read(RccControl)&0x02000000u))return stage;
            Bus::write(RccConfig,(Bus::read(RccConfig)&~3u)|2u);stage=ClockStage::Verify;
        } else if(stage==ClockStage::Verify) {
            const auto cfg=Bus::read(RccConfig), ctl=Bus::read(RccControl), flash=Bus::read(FlashAccess);
            bool ready=(ctl&3u)==3u && !(cfg&0xf0u) && (flash&3u)==(X035?2u:1u);
            if constexpr (!X035)ready=ready && (ctl&0x03000000u)==0x03000000u && (cfg&15u)==10u;
            if(ready)stage=ClockStage::Complete;
        }
        return stage;
    }
    bool abandon() {
        if(stage==ClockStage::Idle||stage==ClockStage::Complete||stage==ClockStage::Abandoned)return false;
        stage=ClockStage::Abandoned;return true;
    }
};
enum class CounterBegin { Accepted, Open, NotOwned, InvalidFrequency, Busy, Exhausted };
enum class CounterRead { Ready, Closed, Stale, Unstable };
// A token identifies a counter epoch, not an atomic resource lease. Calls are
// serialized; caller owns SysTick and keeps HCLK fixed for the epoch.
template<class Bus,bool Wide> class Counter {
    uint32_t generation=0, tick_hz=0;
    bool open=false;
public:
    Counter()=default;
    Counter(const Counter&)=delete;
    Counter& operator=(const Counter&)=delete;
    uint32_t epoch() const {return generation;}
    uint32_t frequency() const {return tick_hz;}
    CounterBegin begin(uint32_t declared_hclk,bool owned,bool divide_eight) {
        if(open)return CounterBegin::Open;
        if(!owned)return CounterBegin::NotOwned;
        if(!declared_hclk || (divide_eight && (declared_hclk<8 || declared_hclk%8)))return CounterBegin::InvalidFrequency;
        if(generation==0xffffffffu)return CounterBegin::Exhausted;
        if(Bus::read(TickControl)!=0)return CounterBegin::Busy;
        Bus::write(TickControl,0);Bus::write(TickStatus,0);Bus::write(TickLow,0);
        if constexpr (Wide)Bus::write(TickHigh,0);
        Bus::write(TickCompareLow,0xffffffffu);
        if constexpr (Wide)Bus::write(TickCompareHigh,0xffffffffu);
        Bus::write(TickControl,divide_eight?1u:5u);
        ++generation;tick_hz=declared_hclk/(divide_eight?8u:1u);open=true;
        return CounterBegin::Accepted;
    }
    bool stop() {
        if(!open)return false;
        Bus::write(TickControl,0);open=false;return true;
    }
    CounterRead low(uint32_t expected,uint32_t& output) const {
        if(!open)return CounterRead::Closed;
        if(expected!=generation)return CounterRead::Stale;
        output=Bus::read(TickLow);return CounterRead::Ready;
    }
    CounterRead sample(uint32_t expected,uint64_t& output,unsigned attempts=3) const {
        if(!open)return CounterRead::Closed;
        if(expected!=generation)return CounterRead::Stale;
        if constexpr (!Wide) {
            output=Bus::read(TickLow);return CounterRead::Ready;
        } else {
            // Bound work independently of input; no unbounded retry on rollover.
            if(attempts>3)attempts=3;
            for(unsigned i=0;i<attempts;++i) {
                const auto high=Bus::read(TickHigh), low=Bus::read(TickLow), verify=Bus::read(TickHigh);
                if(high==verify){output=(uint64_t(high)<<32)|low;return CounterRead::Ready;}
            }
            return CounterRead::Unstable;
        }
    }
};
// Stateless duration conversion, truncating each call. Caller must supply a
// nonzero tick rate and a valid delta/epoch; this does not extend wrap or sleep.
inline uint64_t elapsed_microseconds(uint32_t ticks,uint32_t tick_hz) {
    return (uint64_t(ticks)*1000000u)/tick_hz;
}
} // namespace runtime_experiment
