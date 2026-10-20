#include "flash_scan.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
using namespace flash_experiment;
constexpr uint32_t physical=0x08000000;
struct Geometry {
    static inline unsigned p=0,e=0,u=0;
    static unsigned program(){return p;}
    static unsigned erase(){return e;}
    static unsigned units(){return u;}
    static uint32_t origin(){return physical;}
};
struct Memory {
    static inline std::vector<uint8_t> bytes;
    static inline std::vector<uint32_t> reads;
    static uint8_t read(uint32_t address){
        assert(address>=physical&&address-physical<bytes.size());reads.push_back(address);return bytes[address-physical];
    }
};
static void emit(unsigned status,ReadState phase,ScanPhase scan,bool owned,uint32_t token,unsigned occupied,uint32_t sequence,
                 bool ready,const Snapshot& current,const Snapshot& output,uint32_t writer){
    std::cout<<"{\"status\":"<<status<<",\"phase\":"<<unsigned(phase)<<",\"scan\":"<<unsigned(scan)<<",\"owned\":"<<owned
             <<",\"token\":"<<token<<",\"occupied\":"<<occupied<<",\"sequence\":"<<sequence<<",\"writer\":"<<writer
             <<",\"ready\":"<<ready<<",\"load\":"<<unsigned(current.status)<<",\"generation\":"<<current.generation
             <<",\"bank\":"<<unsigned(current.bank)<<",\"payload\":[";
    for(unsigned i=0;i<current.size;++i)std::cout<<(i?",":"")<<unsigned(current.bytes[i]);
    std::cout<<"],\"out\":["<<output.generation<<','<<unsigned(output.status)<<','<<unsigned(output.bank)<<','<<unsigned(output.size);
    for(unsigned i=0;i<12;++i)std::cout<<','<<unsigned(output.bytes[i]);
    std::cout<<"],\"reads\":[";
    for(unsigned i=0;i<Memory::reads.size();++i)std::cout<<(i?",":"")<<Memory::reads[i];
    std::cout<<"]}\n";
}
template<Kind K,bool Two,bool Commit> void run(){
    RegionTable<2> table;assert(table.configure(physical,unsigned(Memory::bytes.size())));
    LeasedScanner<Memory,Geometry,decltype(table),K,Two,Commit,32> reader;
    int writer_tag=0;uint32_t writer=0,writer_offset=0,writer_size=0;
    Snapshot output{};output.generation=77;output.status=Status::Corrupt;output.bank=99;output.size=8;
    for(auto& byte:output.bytes)byte=42;
    std::string op;uint32_t a,b;
    while(std::cin>>op){
        Memory::reads.clear();unsigned status=99;
        if(op=="begin")status=unsigned(reader.begin(table));
        else if(op=="poll"){std::cin>>a;status=unsigned(reader.poll(a));}
        else if(op=="peek")status=reader.peek(output);
        else if(op=="end")status=reader.end();
        else if(op=="revoke"){std::cin>>a;status=table.release(&reader,a);}
        else if(op=="writer"){
            std::cin>>a>>b;uint32_t next=writer;
            status=unsigned(table.acquire(&writer_tag,physical+a,b,Geometry::erase(),next));
            if(status==0){writer=next;writer_offset=a;writer_size=b;}
        }else if(op=="release"){
            std::cin>>a;status=table.release(&writer_tag,a);
            if(status&&writer==a)writer=0;
        }else if(op=="poke"){
            std::cin>>a>>b;status=table.owns(&writer_tag,writer)&&a<writer_size;
            if(status)Memory::bytes[writer_offset+a]=uint8_t(b);
        }else if(op=="configure"){
            std::cin>>a>>b;status=table.configure(a,b);
        }else assert(op=="state");
        Snapshot current{};const bool ready=reader.peek(current);
        emit(status,reader.phase(),reader.scan_phase(),reader.owned(),reader.lease_ticket(),table.occupied(),table.last_ticket(),ready,current,output,writer);
    }
}
template<class Generation> void ledger(){
    RegionTable<2,Generation> table;int tags[3]{};typename decltype(table)::Ticket returned=99;
    std::string op;uint32_t a,b,c,d;unsigned status=99;
    while(std::cin>>op){
        if(op=="configure"){std::cin>>a>>b;status=table.configure(a,b);}
        else if(op=="claim"){std::cin>>a>>b>>c>>d;assert(a<=3);status=unsigned(table.acquire(a==3?nullptr:&tags[a],b,c,d,returned));}
        else if(op=="release"){std::cin>>a>>b;assert(a<3);status=table.release(&tags[a],Generation(b));}
        else {assert(op=="owns");std::cin>>a>>b;assert(a<3);status=table.owns(&tags[a],Generation(b));}
        std::cout<<"{\"status\":"<<status<<",\"ticket\":"<<unsigned(returned)<<",\"occupied\":"<<table.occupied()<<",\"sequence\":"<<unsigned(table.last_ticket())<<"}\n";
    }
}
int main(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="ledger"){
        if(std::string(argv[2])=="short")ledger<uint8_t>();else ledger<uint32_t>();return 0;
    }
    assert(argc==5);const unsigned candidate=unsigned(std::stoul(argv[1]));
    Geometry::p=unsigned(std::stoul(argv[2]));Geometry::e=unsigned(std::stoul(argv[3]));Geometry::u=unsigned(std::stoul(argv[4]));
    assert(Geometry::e&&Geometry::u<=32767/Geometry::e);Memory::bytes.assign(2*Geometry::e*Geometry::u,255);
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
