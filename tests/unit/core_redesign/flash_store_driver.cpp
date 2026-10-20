#include "flash_store.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace flash_experiment;
struct Geometry {
    static constexpr unsigned max_program_bytes=256;
    static inline unsigned p=0,e=0,u=0;
    static unsigned program(){return p;}
    static unsigned erase(){return e;}
    static unsigned units(){return u;}
};
struct Fake {
    static inline std::vector<uint8_t> bytes;
    static inline std::vector<bool> attempted;
    static inline int cut=-1;
    static inline unsigned reads=0;
    struct Operation { unsigned kind,offset,size;bool success;std::vector<uint8_t> data; };
    static inline std::vector<Operation> events;
    static uint8_t read(uint32_t offset){assert(offset<bytes.size());++reads;return bytes[offset];}
    static uint8_t mask(unsigned i,unsigned length){return cut==256?(i<length/2?255:0):uint8_t(cut);}
    static bool erase(uint32_t offset,unsigned length){
        assert(length==Geometry::e&&offset%length==0&&offset+length<=bytes.size());
        const bool success=cut<0;
        for(unsigned i=0;i<length;++i)bytes[offset+i]|=success?255:mask(i,length);
        for(unsigned i=offset/Geometry::p;i<(offset+length)/Geometry::p;++i)attempted[i]=!success;
        events.push_back({0,offset,length,success,{}});cut=-1;return success;
    }
    static bool program(uint32_t offset,const uint8_t* data,unsigned length){
        assert(length==Geometry::p&&offset%length==0&&offset+length<=bytes.size());
        assert(!attempted[offset/length]);attempted[offset/length]=true;
        const bool success=cut<0;
        for(unsigned i=0;i<length;++i){assert(bytes[offset+i]==255);bytes[offset+i]&=data[i]|(success?0:uint8_t(255^mask(i,length)));}
        events.push_back({1,offset,length,success,std::vector<uint8_t>(data,data+length)});cut=-1;return success;
    }
};
template<Kind K,bool Two,bool Commit> void run(){
    using Driver=Store<Fake,Geometry,K,Two,Commit>;
    auto driver=std::make_unique<Driver>();std::string op;unsigned a,b;uint32_t answer=0xaabbccdd;
    while(std::cin>>op){
        Fake::reads=0;Fake::events.clear();unsigned status=99;
        if(op=="begin"){
            std::cin>>a>>b;assert(a<=512);std::vector<uint8_t> value(a);
            for(auto& byte:value){unsigned input;std::cin>>input;byte=uint8_t(input);}
            status=unsigned(driver->begin(value.data(),a,b!=0));
        }else if(op=="null_begin"){std::cin>>a;status=unsigned(driver->begin(nullptr,a));}
        else if(op=="step")status=unsigned(driver->step());
        else if(op=="abandon")driver->abandon();
        else if(op=="reset"){driver=std::make_unique<Driver>();Fake::cut=-1;}
        else if(op=="fresh"){
            driver=std::make_unique<Driver>();Fake::cut=-1;
            Fake::bytes.assign(Geometry::e*Geometry::u,255);Fake::attempted.assign(Fake::bytes.size()/Geometry::p,false);
        }
        else if(op=="cut"){int value;std::cin>>value;assert(value>=0&&value<=256);Fake::cut=value;}
        else if(op=="poke"){std::cin>>a>>b;assert(a<Fake::bytes.size());Fake::bytes[a]=uint8_t(b);}
        else if(op=="put"){
            std::cin>>a>>b;
            if constexpr(K==Kind::Keys)status=unsigned(driver->put(uint16_t(a),b));
            else assert(false);
        }else if(op=="remove"){
            std::cin>>a;
            if constexpr(K==Kind::Keys)status=unsigned(driver->erase_key(uint16_t(a)));
            else assert(false);
        }else if(op=="get"){
            std::cin>>a;
            if constexpr(K==Kind::Keys)status=driver->get(uint16_t(a),answer);
            else assert(false);
        }else assert(op=="load"||op=="dump");
        const auto read_count=Fake::reads;Snapshot snapshot;driver->load(snapshot);
        uint32_t hash=2166136261u;for(auto byte:Fake::bytes)hash=(hash^byte)*16777619u;
        std::cout<<"{\"status\":"<<status<<",\"supported\":"<<driver->supported()<<",\"active\":"<<driver->active()
                 <<",\"phase\":"<<unsigned(driver->phase())<<",\"load\":"<<unsigned(snapshot.status)<<",\"generation\":"<<snapshot.generation
                 <<",\"bank\":"<<unsigned(snapshot.bank)<<",\"size\":"<<unsigned(snapshot.size)<<",\"hash\":"<<hash<<",\"reads\":"<<read_count<<",\"answer\":"<<answer<<",\"payload\":[";
        for(unsigned i=0;i<snapshot.size;++i)std::cout<<(i?",":"")<<unsigned(snapshot.bytes[i]);
        std::cout<<"],\"events\":[";
        for(unsigned i=0;i<Fake::events.size();++i){const auto& event=Fake::events[i];
            std::cout<<(i?",":"")<<"["<<event.kind<<','<<event.offset<<','<<event.size<<','<<event.success<<",[";
            for(unsigned j=0;j<event.data.size();++j)std::cout<<(j?",":"")<<unsigned(event.data[j]);
            std::cout<<"]]";
        }
        std::cout<<"],\"memory\":[";
        if(op=="dump")for(unsigned i=0;i<Fake::bytes.size();++i)std::cout<<(i?",":"")<<unsigned(Fake::bytes[i]);
        std::cout<<"]}\n";
    }
}
int main(int argc,char** argv){
    assert(argc==5);const unsigned candidate=unsigned(std::stoul(argv[1]));
    Geometry::p=unsigned(std::stoul(argv[2]));Geometry::e=unsigned(std::stoul(argv[3]));Geometry::u=unsigned(std::stoul(argv[4]));
    assert(Geometry::p&&Geometry::e%Geometry::p==0&&Geometry::u<=65535/Geometry::e);
    Fake::bytes.assign(Geometry::e*Geometry::u,255);Fake::attempted.assign(Fake::bytes.size()/Geometry::p,false);
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
