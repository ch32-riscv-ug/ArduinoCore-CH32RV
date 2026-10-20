#include "i2c_resources.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
using namespace i2c_experiment;
#include "i2c_resource_fake.inc"
using namespace i2c_resource_experiment;
struct Pins { bool prepared=false;bool ready() const{return prepared;} };
template<class Counter> int run() {
    Domain<2,Counter> domain;Pins pins;
    using Storage=i2c_experiment::Buffer<64,true>;
    using Device=Port<Fake,Storage,Pins,decltype(domain)>;
    Storage first_buffer,second_buffer;Device first(domain,pins),second(domain,pins);
    Device* devices[]={&first,&second};Storage* buffers[]={&first_buffer,&second_buffer};
    unsigned selected=0;uint8_t external[2]{};typename decltype(domain)::Ticket ticket{99,99};
    i2c_experiment::Completion out{ i2c_experiment::Result::Failure,77,78,79,80 };
    uint32_t notice=99;uint8_t value=123;
    for(std::string line;std::getline(std::cin,line);) {
        assert(!Fake::depth);Fake::events.clear();
        std::istringstream in(line);std::string op;in>>op;unsigned a=0,b=0,c=0,status=99;
        Device& d=*devices[selected];Storage& buffer=*buffers[selected];
        if(op=="select"){in>>a;assert(a<2);selected=a;status=1;}
        else if(op=="change")status=domain.begin_change();
        else if(op=="publish"){in>>a;status=domain.publish(a);}
        else if(op=="pins"){in>>a;status=!(domain.held()&6);if(status)pins.prepared=a!=0;}
        else if(op=="claim"){in>>a>>b;assert(a<2);status=domain.claim(&external[a],b,ticket);}
        else if(op=="release"){
            in>>a>>b>>c;assert(a<2);status=domain.release(&external[a],{Counter(b),uint8_t(c)});
        }
        else if(op=="open"){in>>a;status=unsigned(d.open(a));}
        else if(op=="start"){in>>a>>b>>c;status=unsigned(d.start(&buffer,a,b,c));}
        else if(op=="cancel"){in>>a;status=unsigned(d.cancel(a));}
        else if(op=="take"){in>>a;status=d.take(a,out);}
        else if(op=="end")status=d.end();
        else if(op=="step")d.step();
        else if(op=="peek")status=d.peek(notice);
        else if(op=="ack"){in>>a;status=d.ack(a);}
        else if(op=="write"){in>>a>>b;status=buffer.write(a,uint8_t(b));}
        else if(op=="read"){in>>a;status=buffer.read(a,value);}
        else if(op=="event") {
            in>>a>>b;Fake::regs[i2c_experiment::S1/4]=uint16_t(a);Fake::regs[i2c_experiment::S2/4]=uint16_t(b);Fake::seen=false;
            if(a&i2c_experiment::SB)Fake::regs[0]&=uint16_t(~i2c_experiment::START);
            if(a&i2c_experiment::ARLO)Fake::regs[0]&=uint16_t(~(i2c_experiment::START|i2c_experiment::STOP));
            if(!Fake::fifo.empty())Fake::refresh();
        }else if(op=="push") {
            in>>a;for(unsigned i=0;i<a;++i){in>>b;Fake::fifo.push_back(uint8_t(b));}
            assert(Fake::fifo.size()<=2);Fake::refresh();
        }else if(op=="quiet") {
            in>>a;Fake::regs[0]&=uint16_t(~(i2c_experiment::START|i2c_experiment::STOP));
            Fake::regs[i2c_experiment::S2/4]=a?2:0;Fake::regs[i2c_experiment::S1/4]=i2c_experiment::TXE;
            Fake::refresh();Fake::seen=false;
        }else if(op=="control"){in>>a;Fake::regs[0]=uint16_t(a);}
        else assert(op=="snapshot");
        assert(in);
        const auto& current=*devices[selected];const auto& storage=*buffers[selected];const auto completion=current.observed();
        std::cout<<"{\"status\":"<<status<<",\"selected\":"<<selected<<",\"domain\":["<<domain.frequency()<<','<<domain.epoch()<<','<<domain.count()<<','<<domain.held()<<','<<domain.changing()
                 <<"],\"held\":["<<first.held()<<','<<second.held()<<"],\"prepared\":"<<pins.prepared<<",\"ticket\":["<<unsigned(ticket.generation)<<','<<unsigned(ticket.index)
                 <<"],\"open\":"<<current.opened()<<",\"active\":"<<current.active()<<",\"phase\":"<<current.phase()<<",\"terminal\":"<<current.terminal()<<",\"token\":"<<current.token()
                 <<",\"available\":"<<storage.available()<<",\"result\":"<<unsigned(completion.result)<<",\"issued\":"<<completion.issued<<",\"confirmed\":"<<completion.confirmed<<",\"received\":"<<completion.received<<",\"errors\":"<<completion.flags
                 <<",\"notice\":"<<notice<<",\"value\":"<<unsigned(value)<<",\"out\":["<<unsigned(out.result)<<','<<out.issued<<','<<out.confirmed<<','<<out.received<<','<<out.flags<<"],\"regs\":[";
        for(unsigned i=0;i<8;++i)std::cout<<(i?",":"")<<Fake::regs[i];
        std::cout<<"],\"bytes\":[";for(unsigned i=0;i<64;++i){uint8_t byte=0;bool good=storage.read(i,byte);std::cout<<(i?",":"")<<(good?int(byte):-1);}
        std::cout<<"],\"tx\":[";for(unsigned i=0;i<Fake::transmitted.size();++i)std::cout<<(i?",":"")<<Fake::transmitted[i];
        std::cout<<"],\"addr_ctl\":[";for(unsigned i=0;i<Fake::addr_control.size();++i)std::cout<<(i?",":"")<<Fake::addr_control[i];
        std::cout<<"],\"events\":[";for(unsigned i=0;i<Fake::events.size();++i){auto e=Fake::events[i];std::cout<<(i?",":"")<<'['<<e.kind<<','<<e.offset<<','<<e.value<<']';}
        std::cout<<"]}\n";
    }
    return 0;
}
int main(int argc,char**argv){assert(argc==2);return std::string(argv[1])=="short"?run<uint8_t>():run<uint32_t>();}
