#include "i2c_handoff.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace i2c_experiment;
#include "i2c_handoff_fake.inc"

template<bool Dedicated,class Generation,class BackendGeneration> int run(){
    constexpr unsigned Records=Dedicated?3:2;
    using Driver=i2c_handoff_experiment::Manager<Fake,3,Records,Dedicated,64,Generation,BackendGeneration>;
    Driver driver;typename Driver::Storage buffers[3];typename Driver::Token token{99,99,99};
    Completion output{Result::Failure,77,78,79,80};uint8_t value=123;
    for(std::string line;std::getline(std::cin,line);){
        assert(!Fake::depth);Fake::events.clear();std::istringstream in(line);std::string op;in>>op;
        unsigned a=0,b=0,c=0,d=0,e=0,status=99;
        if(op=="backend"){in>>a>>b>>c;status=unsigned(driver.open_backend(a!=0,b,c));}
        else if(op=="close")status=driver.close_backend();
        else if(op=="open"){in>>a;status=driver.open(a);}
        else if(op=="end"){in>>a;status=driver.end(a);}
        else if(op=="submit"){
            in>>a>>b>>c>>d>>e;assert(b<=3);status=unsigned(driver.submit(a,b==3?nullptr:&buffers[b],c,d,e,token));
        }else if(op=="dispatch")status=unsigned(driver.dispatch(token));
        else if(op=="step")driver.step();
        else if(op=="reap")status=driver.reap();
        else if(op=="cancel"||op=="notice"||op=="ack"||op=="take"){
            in>>a>>b>>c;assert(b<=255&&c<=255);typename Driver::Token request{Generation(a),uint8_t(b),uint8_t(c)};
            if(op=="cancel")status=unsigned(driver.cancel(request));
            else if(op=="notice")status=driver.notice(request);
            else if(op=="ack")status=driver.ack(request);
            else status=driver.take(request,output);
        }else if(op=="write"){in>>a>>b>>c;assert(a<3);status=buffers[a].write(b,uint8_t(c));}
        else if(op=="read"){in>>a>>b;assert(a<3);status=buffers[a].read(b,value);}
        else if(op=="event"){
            in>>a>>b;Fake::regs[S1/4]=uint16_t(a);Fake::regs[S2/4]=uint16_t(b);Fake::seen=false;
            if(a&SB)Fake::regs[C1/4]&=uint16_t(~START);
            if(a&ARLO)Fake::regs[C1/4]&=uint16_t(~(START|STOP));
            if(!Fake::fifo.empty())Fake::refresh();
        }else if(op=="push"){
            in>>a;for(unsigned i=0;i<a;++i){in>>b;Fake::fifo.push_back(uint8_t(b));}assert(Fake::fifo.size()<=2);Fake::refresh();
        }else if(op=="quiet"){
            in>>a;Fake::regs[C1/4]&=uint16_t(~(START|STOP));Fake::regs[S2/4]=a?2:0;Fake::regs[S1/4]=TXE;
            Fake::refresh();Fake::seen=false;
        }else if(op=="control"){in>>a;Fake::regs[C1/4]=uint16_t(a);}
        else assert(op=="snapshot");
        assert(in);
        std::cout<<"{\"status\":"<<status<<",\"token\":["<<unsigned(token.generation)<<','<<unsigned(token.index)<<','<<unsigned(token.client)
                 <<"],\"backend\":["<<driver.backend_opened()<<','<<driver.backend_active()<<','<<driver.backend_terminal()<<','<<driver.backend_phase()<<','<<driver.backend_generation()
                 <<"],\"current\":"<<driver.current()<<",\"cursor\":"<<driver.cursor()<<",\"opened\":[";
        for(unsigned i=0;i<3;++i)std::cout<<(i?",":"")<<driver.opened(i);
        std::cout<<"],\"records\":[";
        for(unsigned i=0;i<Records;++i)std::cout<<(i?",":"")<<'['<<driver.phase(i)<<','<<driver.owner(i)<<','<<driver.generation(i)<<']';
        std::cout<<"],\"out\":["<<unsigned(output.result)<<','<<output.issued<<','<<output.confirmed<<','<<output.received<<','<<output.flags<<"],\"value\":"<<unsigned(value)<<",\"bytes\":[";
        for(unsigned bfr=0;bfr<3;++bfr){
            std::cout<<(bfr?",":"")<<'[';
            for(unsigned i=0;i<64;++i){uint8_t v=0;const bool available=buffers[bfr].read(i,v);std::cout<<(i?",":"")<<(available?int(v):-1);}
            std::cout<<']';
        }
        std::cout<<"],\"regs\":[";for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<Fake::regs[i];
        std::cout<<"],\"tx\":[";for(unsigned i=0;i<Fake::transmitted.size();++i)std::cout<<(i?",":"")<<Fake::transmitted[i];
        std::cout<<"],\"addr_ctl\":[";for(unsigned i=0;i<Fake::addr_control.size();++i)std::cout<<(i?",":"")<<Fake::addr_control[i];
        std::cout<<"],\"events\":[";
        for(unsigned i=0;i<Fake::events.size();++i){auto event=Fake::events[i];std::cout<<(i?",":"")<<'['<<event.kind<<','<<event.offset<<','<<event.value<<']';}
        std::cout<<"]}\n";
    }
    return 0;
}
template<bool D> int generations(const std::string& mode){
    if(mode=="request8")return run<D,uint8_t,uint32_t>();
    if(mode=="backend8")return run<D,uint32_t,uint8_t>();
    assert(mode=="normal");return run<D,uint32_t,uint32_t>();
}
int main(int argc,char** argv){
    assert(argc==3);if(std::string(argv[1])=="dedicated")return generations<true>(argv[2]);
    assert(std::string(argv[1])=="pooled");return generations<false>(argv[2]);
}
