#include "sleep_work.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
using namespace sleep_work_experiment;
template<unsigned C,unsigned N,bool Timed,bool Safe> int run(unsigned domain,bool opt) {
    Application<C,N,Timed,Safe> app;assert(app.begin(WorkDomain(domain),opt));
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream input(line);std::string command;uint64_t a[6]{};input>>command;for(auto& n:a)input>>n;
        int status=99;const unsigned side=unsigned(a[0]);
        if(command=="job"){assert(side<2);status=app.workers[side].submit(unsigned(a[1]));}
        else if(command=="start"){assert(side<2);status=int(app.workers[side].port.start(unsigned(a[1])));}
        else if(command=="finish") {
            assert(side<2);status=app.workers[side].port.finish(uint32_t(a[1]),IoResult(a[2]),unsigned(a[3]),uint8_t(a[4]),a[5]!=0);
        }
        else if(command=="turn")status=int(app.opportunity(side,WorkOpportunity(a[1]),WorkProfile(a[2]),WorkContext(a[3])));
        else if(command=="request")status=app.flow.request(app.attempt);
        else if(command=="poll")status=int(app.flow.poll());
        else if(command=="enter")status=int(app.flow.enter(uint32_t(a[0]),a[1]!=0,a[2]!=0,Injection(a[3])));
        else if(command=="wake")status=app.flow.wake(uint32_t(a[0]));
        else if(command=="post")app.flow.post_input();
        else if(command=="take_input")status=app.flow.take_input();
        else if(command=="abort")status=app.flow.abort();
        else if(command=="retry")status=app.flow.retry();
        else if(command=="restore") {
            app.source_status=app.flow.restore_clock(uint32_t(a[0]),{a[2],uint32_t(a[1]),6000000},a[3]!=0,a[4],a[5],app.stamp);
            status=int(app.source_status);
        }
        else if(command=="ready"){assert(side<2 && a[1]<3);app.workers[side].port.restoration=Reply(a[1]);}
        else if(command=="observe") {
            app.source_status=app.clock.observe({a[1],uint32_t(a[0]),6000000},{a[2],true},app.stamp);status=int(app.source_status);
        }
        else if(command=="arm") {
            if constexpr(Timed)status=app.timer.arm(app.source_status,app.stamp,uint32_t(a[0]),uint32_t(a[1]));
            else status=-1;
        }
        else if(command=="timer_poll") {
            if constexpr(Timed)status=int(app.timer.poll(app.clock.phase()==time_experiment::State::Paused?time_experiment::Result::Paused:app.source_status,app.stamp));
            else status=-1;
        }
        else if(command=="timer_take") {if constexpr(Timed)status=app.timer.take(app.notice);else status=-1;}
        else assert(command=="inspect");
        std::cout<<"{\"status\":"<<status<<",\"phase\":"<<unsigned(app.flow.phase())<<",\"attempt\":"<<app.attempt
                 <<",\"frozen\":"<<app.list.frozen()<<",\"input\":"<<app.flow.input_pending()<<",\"clock_ready\":"<<app.flow.clock_ready()
                 <<",\"clock\":["<<unsigned(app.clock.phase())<<','<<app.clock.history_us()<<','<<app.clock.epoch()<<','<<app.clock.source()
                 <<"],\"stamp\":["<<app.stamp.us<<','<<app.stamp.epoch<<','<<unsigned(app.stamp.quality)<<"],\"timer\":[";
        if constexpr(Timed)std::cout<<app.timer.armed()<<','<<app.timer.pending()<<','<<app.timer.epoch();else std::cout<<"0,0,0";
        std::cout<<"],\"notice\":["<<app.notice.expiration.latest_due<<','<<app.notice.expiration.skipped<<','<<app.notice.epoch<<"],\"workers\":[";
        for(unsigned i=0;i<2;++i) {
            auto& w=app.workers[i];auto& p=w.port;std::cout<<(i?",":"")<<'['<<p.powered<<','<<p.admitting<<','<<p.io.opened()<<','<<p.io.active()<<','<<p.io.terminal()<<','<<p.io.cancel_pending()<<','<<p.buffer.available()<<','<<p.io.token()<<','
                <<unsigned(w.remaining)<<','<<w.completed_steps<<','<<w.collected<<','<<w.calls<<','<<unsigned(w.completion.result)<<','<<w.completion.transferred<<']';
        }
        std::cout<<"]}\n";
    }
    return 0;
}
template<unsigned C,unsigned N,bool Timed> int safety(bool safe,unsigned d,bool opt){return safe?run<C,N,Timed,true>(d,opt):run<C,N,Timed,false>(d,opt);}
template<unsigned C,unsigned N> int timed(bool t,bool s,unsigned d,bool opt){return t?safety<C,N,true>(s,d,opt):safety<C,N,false>(s,d,opt);}
template<unsigned C> int capacity(unsigned n,bool t,bool s,unsigned d,bool opt){if(!n)return timed<C,0>(t,s,d,opt);if(n==2)return timed<C,2>(t,s,d,opt);assert(n==4);return timed<C,4>(t,s,d,opt);}
int main(int argc,char** argv) {
    assert(argc==7);unsigned c=unsigned(std::strtoul(argv[1],nullptr,10)),n=unsigned(std::strtoul(argv[2],nullptr,10)),d=unsigned(std::strtoul(argv[5],nullptr,10));
    bool t=std::strtoul(argv[3],nullptr,10)!=0,s=std::strtoul(argv[4],nullptr,10)!=0,opt=std::strtoul(argv[6],nullptr,10)!=0;assert(d<2);
    if(c==1)return capacity<1>(n,t,s,d,opt);
    if(c==2)return capacity<2>(n,t,s,d,opt);
    if(c==3)return capacity<3>(n,t,s,d,opt);
    assert(c==4);return capacity<4>(n,t,s,d,opt);
}
