#pragma once
#include "flash_integrated.hpp"

namespace experiment {
enum class SaveRequest : uint8_t { Accepted,Disabled,NotReady,ResultHeld,Rejected };
// N05 glue only. Blob8 is a comparison condition, not the selected persistence
// API/format. One unit overwrites one bank; 2/4 units use two banks, no footer.
template<class Media,class Geometry,unsigned Units> struct SaveCoexistence {
    static_assert(Units==1 || Units==2 || Units==4,"fixed capacity comparison");
    using Table=flash_experiment::RegionTable<1>;
    using Driver=flash_experiment::IntegratedStore<Media,Geometry,Table,flash_experiment::Kind::Blob,(Units>1),false,32>;
    Table table;
    Driver driver;
    bool result_held=false,result_ok=false;
    uint32_t completed=0,collected=0;
    bool open() {
        return table.configure(Geometry::origin(),Geometry::erase()*Units) &&
               driver.open(table)==flash_experiment::Claim::Accepted;
    }
    bool owned() const { return driver.owned(); }
    SaveRequest request(uint32_t value,uint32_t tag,bool recover=false) {
        if(result_held)return SaveRequest::ResultHeld;
        if(driver.phase()!=flash_experiment::UpdateState::Ready)return SaveRequest::NotReady;
        uint8_t bytes[8];
        for(unsigned i=0;i<4;++i) { bytes[i]=uint8_t(value>>(8*i));bytes[4+i]=uint8_t(tag>>(8*i)); }
        return driver.begin_update(bytes,sizeof(bytes),recover)==flash_experiment::UpdateStart::Accepted ?
               SaveRequest::Accepted : SaveRequest::Rejected;
    }
    void progress() {
        if(result_held)return;
        if(driver.phase()==flash_experiment::UpdateState::Scanning)driver.poll(32);
        else if(driver.phase()==flash_experiment::UpdateState::Writing) {
            const auto result=driver.write_step();
            if(result==flash_experiment::UpdateState::Done || result==flash_experiment::UpdateState::Failed) {
                result_held=true;result_ok=result==flash_experiment::UpdateState::Done;++completed;
            }
        }
    }
    bool take(bool& success) {
        if(!result_held || !owned())return false;
        success=result_ok;result_held=false;++collected;
        return driver.rescan();
    }
    bool peek(flash_experiment::Snapshot& out) const { return driver.peek(out); }
    bool end() { result_held=false;return driver.end(); }
};
template<class Media,class Geometry> struct SaveCoexistence<Media,Geometry,0> {
    bool open(){return false;}
    bool owned()const{return false;}
    SaveRequest request(uint32_t,uint32_t,bool=false){return SaveRequest::Disabled;}
    void progress(){}
    bool take(bool&){return false;}
    bool peek(flash_experiment::Snapshot&)const{return false;}
    bool end(){return false;}
};
} // namespace experiment
