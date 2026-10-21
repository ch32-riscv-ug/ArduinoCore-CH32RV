#include "completion_arbiter.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
using namespace completion_arbiter_experiment;
template<bool Dedicated,class Generation> int run() {
    constexpr unsigned Records=Dedicated?3:2;
    Arbiter<3,Records,Dedicated,32,Generation> arbiter;
    typename decltype(arbiter)::Buffer buffers[3];
    typename decltype(arbiter)::Token output{99,99,99};
    typename decltype(arbiter)::Completion completion{experiment::IoResult::Failure,99,99};
    uint8_t byte=99;
    for(std::string line;std::getline(std::cin,line);) {
        std::istringstream in(line);std::string op;in>>op;
        unsigned a=0,b=0,c=0,d=0,e=0,f=0,status=0;
        auto token=[&] {in>>a>>b>>c;return typename decltype(arbiter)::Token{Generation(a),uint8_t(b),uint8_t(c)};};
        if(op=="open"){in>>a;status=arbiter.open(a);}
        else if(op=="end"){in>>a;status=arbiter.end(a);}
        else if(op=="submit") {
            in>>a>>b>>c>>d>>e;assert(b<3);
            status=unsigned(arbiter.submit(a,&buffers[b],c,experiment::IoMode(d),e,output));
        }else if(op=="dispatch")status=arbiter.dispatch(output);
        else if(op=="finish") {
            const auto t=token();in>>d>>e>>f>>a;
            status=arbiter.finish(t,experiment::IoResult(d),e,uint8_t(f),a!=0);
        }else if(op=="cancel")status=unsigned(arbiter.cancel(token()));
        else if(op=="notice")status=arbiter.notice(token());
        else if(op=="ack")status=arbiter.ack(token());
        else if(op=="take")status=arbiter.take(token(),completion);
        else if(op=="write"){in>>a>>b>>c;assert(a<3);status=buffers[a].write(b,uint8_t(c));}
        else if(op=="read"){in>>a>>b;assert(a<3);status=buffers[a].read(b,byte);}
        else assert(op=="snapshot");
        assert(in);
        std::cout<<"{\"status\":"<<status<<",\"token\":["<<unsigned(output.generation)<<','<<unsigned(output.index)<<','<<unsigned(output.client)
                 <<"],\"completion\":["<<unsigned(completion.result)<<','<<completion.transferred<<','<<completion.digest
                 <<"],\"byte\":"<<unsigned(byte)<<",\"cursor\":"<<arbiter.cursor()<<",\"current\":"<<arbiter.current()<<",\"opened\":[";
        for(unsigned i=0;i<3;++i)std::cout<<(i?",":"")<<arbiter.opened(i);
        std::cout<<"],\"records\":[";
        for(unsigned i=0;i<Records;++i)std::cout<<(i?",":"")<<'['<<arbiter.phase(i)<<','<<arbiter.owner(i)<<','<<arbiter.generation(i)<<','<<arbiter.pending(i)<<','<<arbiter.cancelled(i)<<']';
        std::cout<<"],\"buffers\":[";
        for(unsigned i=0;i<3;++i){std::cout<<(i?",":"")<<'[';for(unsigned j=0;j<32;++j){uint8_t value=0;bool available=buffers[i].read(j,value);std::cout<<(j?",":"")<<(available?int(value):-1);}std::cout<<']';}
        std::cout<<"]}\n";
    }
    return 0;
}
int main(int argc,char**argv) {
    assert(argc==3);const bool dedicated=std::string(argv[1])=="dedicated",short_counter=std::string(argv[2])=="short";
    if(dedicated)return short_counter?run<true,uint8_t>():run<true,uint32_t>();
    return short_counter?run<false,uint8_t>():run<false,uint32_t>();
}
