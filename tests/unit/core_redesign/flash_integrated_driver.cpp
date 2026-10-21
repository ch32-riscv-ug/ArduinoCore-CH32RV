#include "flash_integrated.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace flash_experiment;
constexpr uint32_t physical=0x08000000;
struct Geometry {
    static constexpr unsigned max_program_bytes=256;
    static inline unsigned p=0,e=0,u=0;
    static unsigned program(){return p;}
    static unsigned erase(){return e;}
    static unsigned units(){return u;}
    static uint32_t origin(){return physical;}
};
struct Memory {
    static inline std::vector<uint8_t> bytes;
    static inline std::vector<bool> attempted;
    static inline int cut=-1;
    static inline std::vector<uint32_t> reads;
    static inline bool (*allowed)()=nullptr;
    struct Operation { unsigned kind,offset,size;bool success;std::vector<uint8_t> data; };
    static inline std::vector<Operation> events;
    static uint8_t read(uint32_t address){assert(allowed&&allowed());assert(address>=physical&&address-physical<bytes.size());reads.push_back(address);return bytes[address-physical];}
    static uint8_t mask(unsigned i,unsigned length){return cut==256?(i<length/2?255:0):uint8_t(cut);}
    static bool erase(uint32_t address,unsigned length){
        assert(allowed&&allowed()&&address>=physical);const auto offset=address-physical;
        assert(length==Geometry::e&&offset%length==0&&offset+length<=bytes.size());
        const bool success=cut<0;
        for(unsigned i=0;i<length;++i)bytes[offset+i]|=success?255:mask(i,length);
        for(unsigned i=offset/Geometry::p;i<(offset+length)/Geometry::p;++i)attempted[i]=!success;
        events.push_back({0,offset,length,success,{}});cut=-1;return success;
    }
    static bool program(uint32_t address,const uint8_t* data,unsigned length){
        assert(allowed&&allowed()&&address>=physical);const auto offset=address-physical;
        assert(length==Geometry::p&&offset%length==0&&offset+length<=bytes.size());
        assert(!attempted[offset/length]);attempted[offset/length]=true;
        const bool success=cut<0;
        for(unsigned i=0;i<length;++i){assert(bytes[offset+i]==255);bytes[offset+i]&=data[i]|(success?0:uint8_t(255^mask(i,length)));}
        events.push_back({1,offset,length,success,std::vector<uint8_t>(data,data+length)});cut=-1;return success;
    }
};
template<Kind K,bool Two,bool Commit> void run(){
    RegionTable<2> table;assert(table.configure(physical,unsigned(Memory::bytes.size()*2)));
    using Driver=IntegratedStore<Memory,Geometry,decltype(table),K,Two,Commit>;
    Driver driver;static Driver* live=&driver;Memory::allowed=[](){return live->owned();};
    int external_tag=0;uint32_t external=0,offset=0,length=0,answer=0xaabbccdd;
    Snapshot output{};output.generation=77;output.status=Status::Corrupt;output.bank=99;output.size=8;
    for(auto& byte:output.bytes)byte=42;
    std::string op;unsigned a,b;
    while(std::cin>>op){
        Memory::reads.clear();Memory::events.clear();unsigned status=99;
        if(op=="open")status=unsigned(driver.open(table));
        else if(op=="poll"){std::cin>>a;status=unsigned(driver.poll(a));}
        else if(op=="step")status=unsigned(driver.write_step());
        else if(op=="peek")status=driver.peek(output);
        else if(op=="end")status=driver.end();
        else if(op=="rescan")status=driver.rescan();
        else if(op=="begin"){
            std::cin>>a>>b;assert(a<=512);std::vector<uint8_t> data(a);
            for(auto& byte:data){unsigned value;std::cin>>value;byte=uint8_t(value);}
            status=unsigned(driver.begin_update(data.data(),a,b!=0));
        }else if(op=="null_begin"){std::cin>>a;status=unsigned(driver.begin_update(nullptr,a));}
        else if(op=="put"){
            std::cin>>a>>b;if constexpr(K==Kind::Keys)status=unsigned(driver.put(uint16_t(a),b));else assert(false);
        }else if(op=="remove"){
            std::cin>>a;if constexpr(K==Kind::Keys)status=unsigned(driver.erase_key(uint16_t(a)));else assert(false);
        }else if(op=="get"){
            std::cin>>a;if constexpr(K==Kind::Keys)status=driver.get(uint16_t(a),answer);else assert(false);
        }else if(op=="revoke"){std::cin>>a;status=table.release(&driver,a);}
        else if(op=="claim"){
            std::cin>>a>>b;uint32_t next=external;
            status=unsigned(table.acquire(&external_tag,physical+a,b,Geometry::erase(),next));
            if(status==0){external=next;offset=a;length=b;}
        }else if(op=="release"){
            std::cin>>a;status=table.release(&external_tag,a);if(status&&external==a)external=0;
        }else if(op=="poke"){
            std::cin>>a>>b;status=table.owns(&external_tag,external)&&a<length&&offset+a<Memory::bytes.size();
            if(status)Memory::bytes[offset+a]=uint8_t(b);
        }else if(op=="cut"){
            int value;std::cin>>value;assert(value>=0&&value<=256);Memory::cut=value;
        }else assert(op=="state"||op=="dump");
        Snapshot current{};const bool ready=driver.peek(current);
        uint32_t hash=2166136261u;for(auto byte:Memory::bytes)hash=(hash^byte)*16777619u;
        std::cout<<"{\"status\":"<<status<<",\"supported\":"<<driver.supported()<<",\"phase\":"<<unsigned(driver.phase())
                 <<",\"scan\":"<<unsigned(driver.scan_phase())<<",\"write\":"<<unsigned(driver.write_phase())
                 <<",\"owned\":"<<driver.owned()<<",\"ticket\":"<<driver.lease_ticket()<<",\"occupied\":"<<table.occupied()
                 <<",\"sequence\":"<<table.last_ticket()<<",\"external\":"<<external<<",\"ready\":"<<ready
                 <<",\"load\":"<<unsigned(current.status)<<",\"generation\":"<<current.generation<<",\"bank\":"<<unsigned(current.bank)<<",\"payload\":[";
        for(unsigned i=0;i<current.size;++i)std::cout<<(i?",":"")<<unsigned(current.bytes[i]);
        std::cout<<"],\"out\":["<<output.generation<<','<<unsigned(output.status)<<','<<unsigned(output.bank)<<','<<unsigned(output.size);
        for(auto byte:output.bytes)std::cout<<','<<unsigned(byte);
        std::cout<<"],\"answer\":"<<answer<<",\"hash\":"<<hash<<",\"reads\":[";
        for(unsigned i=0;i<Memory::reads.size();++i)std::cout<<(i?",":"")<<Memory::reads[i];
        std::cout<<"],\"events\":[";
        for(unsigned i=0;i<Memory::events.size();++i){const auto& e=Memory::events[i];
            std::cout<<(i?",":"")<<'['<<e.kind<<','<<e.offset<<','<<e.size<<','<<e.success<<",[";
            for(unsigned j=0;j<e.data.size();++j)std::cout<<(j?",":"")<<unsigned(e.data[j]);
            std::cout<<"]]";
        }
        std::cout<<"],\"memory\":[";
        if(op=="dump")for(unsigned i=0;i<Memory::bytes.size();++i)std::cout<<(i?",":"")<<unsigned(Memory::bytes[i]);
        std::cout<<"]}\n";
    }
}
int main(int argc,char** argv){
    assert(argc==5);const unsigned candidate=unsigned(std::stoul(argv[1]));
    Geometry::p=unsigned(std::stoul(argv[2]));Geometry::e=unsigned(std::stoul(argv[3]));Geometry::u=unsigned(std::stoul(argv[4]));
    assert(Geometry::p&&Geometry::e%Geometry::p==0&&Geometry::u<=32767/Geometry::e);
    Memory::bytes.assign(Geometry::e*Geometry::u,255);Memory::attempted.assign(Memory::bytes.size()/Geometry::p,false);
    switch(candidate){
    case 0:run<Kind::Raw,false,false>();break;
    case 1:run<Kind::Blob,false,false>();break;
    case 2:run<Kind::Blob,true,false>();break;
    case 3:run<Kind::Keys,false,false>();break;
    case 4:run<Kind::Keys,true,false>();break;
    case 5:run<Kind::Blob,true,true>();break;
    case 6:run<Kind::Keys,true,true>();break;
    default:assert(false);
    }
}
