#include "logical_time.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace time_experiment;
template<unsigned Bits,bool Assumed,class Conversion,class Epoch> void run(){
    LogicalClock<Bits,Assumed,Conversion,Epoch> clock;
    DomainTimer<false,false> strict;
    DomainTimer<true,true> relaxed;
    Stamp output{123,77,99};Dropped drop{55,66};Notice notice;
    notice.expiration={77,78,true};notice.epoch=79;
    Result last=Result::Closed;
    std::string op;Sample sample{};uint64_t value=0;uint32_t a=0,b=0;unsigned flag=0;
    while(std::cin>>op){
        unsigned status=99;
        if(op=="begin"){
            std::cin>>sample.epoch>>sample.raw>>sample.hz>>value;
            last=clock.begin(sample,value,output);status=unsigned(last);
        }else if(op=="observe"){
            std::cin>>sample.epoch>>sample.raw>>sample.hz>>flag>>value;
            last=clock.observe(sample,Gap{value,flag!=0},output);status=unsigned(last);
        }else if(op=="pause")status=clock.pause();
        else if(op=="stop")status=clock.stop();
        else if(op=="known"){
            std::cin>>sample.epoch>>sample.raw>>sample.hz>>value>>flag;
            last=clock.resume_known(sample,value,flag!=0,output,drop);status=unsigned(last);
        }else if(op=="unknown"){
            std::cin>>sample.epoch>>sample.raw>>sample.hz>>value;
            last=clock.resume_unknown(sample,value,output);status=unsigned(last);
        }else if(op=="arm"){
            std::cin>>flag>>a>>b;status=flag?relaxed.arm(last,output,a,b):strict.arm(last,output,a,b);
        }else if(op=="poll"){
            std::cin>>flag;status=unsigned(flag?relaxed.poll(last,output):strict.poll(last,output));
        }else if(op=="take"){
            std::cin>>flag;status=flag?relaxed.take(notice):strict.take(notice);
        }else if(op=="stamp"){
            unsigned quality;std::cin>>value>>a>>quality>>b;
            output={value,a,uint8_t(quality)};last=Result(b);
        }else assert(op=="state");
        std::cout<<"{\"status\":"<<status<<",\"last\":"<<unsigned(last)<<",\"phase\":"<<unsigned(clock.phase())
                 <<",\"us\":"<<clock.history_us()<<",\"raw\":"<<clock.history_raw()<<",\"epoch\":"<<clock.epoch()<<",\"source\":"<<clock.source()
                 <<",\"hz\":"<<clock.frequency()<<",\"fraction\":"<<clock.remainder()<<",\"denominator\":"<<clock.remainder_denominator()
                 <<",\"quality\":"<<unsigned(clock.history_quality())<<",\"out\":["<<output.us<<','<<output.epoch<<','<<unsigned(output.quality)
                 <<"],\"drop\":["<<drop.numerator<<','<<drop.denominator<<"],\"strict\":["<<strict.armed()<<','<<strict.pending()<<','<<strict.epoch()
                 <<"],\"relaxed\":["<<relaxed.armed()<<','<<relaxed.pending()<<','<<relaxed.epoch()<<"],\"notice\":["<<notice.expiration.latest_due<<','<<notice.expiration.skipped<<','<<notice.expiration.saturated<<','<<notice.epoch<<"]}\n";
    }
}
template<class Epoch> void select(unsigned variant){
    switch(variant){
    case 0:run<32,false,VariableRate,Epoch>();break;
    case 1:run<32,true,VariableRate,Epoch>();break;
    case 2:run<64,false,VariableRate,Epoch>();break;
    case 3:run<32,false,SixMHz,Epoch>();break;
    case 4:run<32,true,SixMHz,Epoch>();break;
    case 5:run<64,false,SixMHz,Epoch>();break;
    default:assert(false);
    }
}
int main(int argc,char** argv){assert(argc==3);const auto variant=unsigned(std::stoul(argv[1]));if(std::string(argv[2])=="short")select<uint8_t>(variant);else select<uint32_t>(variant);}
