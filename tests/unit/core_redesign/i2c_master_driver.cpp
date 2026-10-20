#include "../../platform/core_redesign/runtime/i2c_master.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
#include <vector>
using namespace i2c_experiment;
struct Fake {
    struct Event{unsigned kind,offset,value;};
    static inline uint16_t regs[8]{};
    static inline bool seen=false;
    static inline unsigned depth=0;
    static inline std::deque<uint8_t> fifo;
    static inline std::vector<Event> events;
    static inline std::vector<unsigned> transmitted,addr_control;
    static void refresh(){regs[S1/4]=uint16_t((regs[S1/4]&~(RXNE|BTF))|(fifo.empty()?0:RXNE)|(fifo.size()==2?BTF:0));}
    static uint16_t read16(uintptr_t p){
        assert(p>=Base&&p<Base+32&&!(p%4));const unsigned off=unsigned(p-Base);
        uint16_t value=regs[off/4];
        if(off==Data){assert(!fifo.empty());value=fifo.front();fifo.pop_front();refresh();seen=false;}
        events.push_back({0,off,value});
        if(off==S1)seen=true;
        if(off==S2&&seen){if(regs[S1/4]&ADDR)addr_control.push_back(regs[C1/4]);regs[S1/4]&=uint16_t(~ADDR);seen=false;}
        return value;
    }
    static void write16(uintptr_t p,uint16_t value){
        assert(p>=Base&&p<Base+32&&!(p%4));const unsigned off=unsigned(p-Base);events.push_back({1,off,value});
        if(off==S1)regs[S1/4]&=uint16_t(value|~Errors);
        else if(off==Data){transmitted.push_back(value&255);regs[S1/4]&=uint16_t(~(SB|TXE|BTF));seen=false;}
        else regs[off/4]=value;
    }
    static unsigned critical_enter(){assert(!depth);++depth;events.push_back({2,0,0});return 0;}
    static void critical_leave(unsigned){assert(depth==1);--depth;events.push_back({3,0,0});}
};
template<bool Guarded,class Generation> void run(){
    Buffer<64,Guarded> buffer;Master<Fake,decltype(buffer),Generation> driver;
    using Token=typename decltype(driver)::Token;
    Completion out{Result::Failure,77,78,79,80};Token notice=99;
    std::string op;unsigned a,b,c;uint8_t value=123;
    while(std::cin>>op){
        assert(!Fake::depth);Fake::events.clear();unsigned status=99;
        if(op=="open"){std::cin>>a>>b>>c;status=unsigned(driver.open(a!=0,b,c));}
        else if(op=="start"){std::cin>>a>>b>>c;status=unsigned(driver.start(&buffer,a,b,c));}
        else if(op=="null_start"){std::cin>>a>>b>>c;status=unsigned(driver.start(nullptr,a,b,c));}
        else if(op=="cancel"){std::cin>>a;status=unsigned(driver.cancel(Token(a)));}
        else if(op=="step")driver.step();
        else if(op=="end")status=driver.end();
        else if(op=="take"){std::cin>>a;status=driver.take(Token(a),out);}
        else if(op=="peek")status=driver.peek(notice);
        else if(op=="ack"){std::cin>>a;status=driver.ack(Token(a));}
        else if(op=="write"){std::cin>>a>>b;status=buffer.write(a,uint8_t(b));}
        else if(op=="read"){std::cin>>a;status=buffer.read(a,value);}
        else if(op=="event"){
            std::cin>>a>>b;Fake::regs[S1/4]=uint16_t(a);Fake::regs[S2/4]=uint16_t(b);Fake::seen=false;
            if(a&SB)Fake::regs[C1/4]&=uint16_t(~START);
            if(a&ARLO)Fake::regs[C1/4]&=uint16_t(~(START|STOP));
            if(!Fake::fifo.empty())Fake::refresh();
        }else if(op=="push"){
            std::cin>>a;for(unsigned i=0;i<a;++i){std::cin>>b;Fake::fifo.push_back(uint8_t(b));}assert(Fake::fifo.size()<=2);Fake::refresh();
        }else if(op=="quiet"){
            std::cin>>a;Fake::regs[C1/4]&=uint16_t(~(START|STOP));Fake::regs[S2/4]=a?2:0;Fake::regs[S1/4]=TXE;
            Fake::refresh();Fake::seen=false;
        }else{assert(op=="control");std::cin>>a;Fake::regs[C1/4]=uint16_t(a);}
        const auto d=driver.observed();
        std::cout<<"{\"status\":"<<status<<",\"open\":"<<driver.opened()<<",\"active\":"<<driver.active()<<",\"phase\":"<<unsigned(driver.phase())
                 <<",\"terminal\":"<<driver.terminal_ready()<<",\"cancel\":"<<driver.cancel_pending()<<",\"pending\":"<<driver.notice_pending()
                 <<",\"token\":"<<unsigned(driver.token())<<",\"result\":"<<unsigned(d.result)<<",\"issued\":"<<d.issued<<",\"confirmed\":"<<d.confirmed<<",\"received\":"<<d.received<<",\"errors\":"<<d.flags
                 <<",\"available\":"<<buffer.available()<<",\"notice\":"<<unsigned(notice)<<",\"value\":"<<unsigned(value)<<",\"out\":["<<unsigned(out.result)<<','<<out.issued<<','<<out.confirmed<<','<<out.received<<','<<out.flags<<"],\"regs\":[";
        for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<Fake::regs[i];
        std::cout<<"],\"bytes\":[";
        for(unsigned i=0;i<64;++i){uint8_t byte=0;const bool good=buffer.read(i,byte);std::cout<<(i?",":"")<<(good?int(byte):-1);}
        std::cout<<"],\"tx\":[";for(unsigned i=0;i<Fake::transmitted.size();++i)std::cout<<(i?",":"")<<Fake::transmitted[i];
        std::cout<<"],\"addr_ctl\":[";for(unsigned i=0;i<Fake::addr_control.size();++i)std::cout<<(i?",":"")<<Fake::addr_control[i];
        std::cout<<"],\"events\":[";for(unsigned i=0;i<Fake::events.size();++i){auto e=Fake::events[i];std::cout<<(i?",":"")<<'['<<e.kind<<','<<e.offset<<','<<e.value<<']';}
        std::cout<<"]}\n";
    }
}
int main(int argc,char**argv){assert(argc==3);const bool guarded=std::string(argv[1])=="guarded",short_generation=std::string(argv[2])=="short";
 if(guarded){if(short_generation)run<true,uint8_t>();else run<true,uint32_t>();}
 else{assert(std::string(argv[1])=="raw");if(short_generation)run<false,uint8_t>();else run<false,uint32_t>();}}
