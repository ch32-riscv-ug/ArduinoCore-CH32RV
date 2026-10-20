#include "display_parts.hpp"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
using namespace display_experiment;

template<class Panel> void run() {
    const View first{5020,5000,5070,1200,true,false,true};
    const View next{9000,0,-1,0,false,true,false};
    Screen<Panel> screen;
    assert(!screen.prepare_chunk() && !screen.mark_started() && !screen.finish(true));
    screen.update(first); screen.step();
    assert(screen.prepare_chunk());
    uint8_t before[Panel::chunk];
    for (unsigned i = 0; i < Panel::chunk; ++i) before[i] = screen.tx[i];
    assert(screen.mark_started());
    screen.update(next);
    assert(!screen.prepare_chunk() && !screen.mark_started());
    for (unsigned i = 0; i < Panel::chunk; ++i) assert(before[i] == screen.tx[i]);
    assert(screen.finish(true) && !screen.finish(false)); screen.step();
    unsigned chunks = 1, tail = Panel::chunk;
    while (screen.frame_count == 0) {
        assert(chunks < (Panel::bytes+Panel::chunk-1)/Panel::chunk);
        tail = screen.chunk_size();
        assert(screen.prepare_chunk() && screen.mark_started() && screen.finish(true));
        screen.step(); ++chunks;
    }
    const uint32_t first_hash = screen.displayed_hash;
    assert(screen.frame_revision != screen.revision);
    screen.now = 100; screen.step();
    for (unsigned n = 0; n < (Panel::bytes+Panel::chunk-1)/Panel::chunk; ++n) {
        assert(screen.prepare_chunk() && screen.mark_started() && screen.finish(true)); screen.step();
    }
    assert(screen.frame_count == 2 && screen.displayed_revision == screen.revision && !screen.frame);
    Screen<Panel> failed;
    failed.update(next); failed.step();
    assert(failed.prepare_chunk() && failed.mark_started() && failed.finish(false)); failed.step();
    assert(!failed.frame && !failed.frame_count && !failed.displayed_hash);
    failed.now = 100; failed.step();
    for (unsigned n = 0; n < (Panel::bytes+Panel::chunk-1)/Panel::chunk; ++n) {
        assert(failed.prepare_chunk() && failed.mark_started() && failed.finish(true)); failed.step();
    }
    assert(failed.frame_count == 1 && failed.displayed_hash == screen.displayed_hash);
    printf("{\"bytes\":%u,\"capacity\":%u,\"chunks\":%u,\"tail\":%u,\"first_hash\":%u,\"next_hash\":%u}\n",
           Panel::bytes,Panel::chunk,chunks,tail,first_hash,screen.displayed_hash);
}
int main(int argc,char** argv) {
    assert(argc == 2);
    switch (strtoul(argv[1],nullptr,10)) {
    case 0: run<Mono64<32>>(); break;
    case 1: run<Compact32>(); break;
    case 2: run<Mono64<16>>(); break;
    case 3: run<Rgb96>(); break;
    default: return 2;
    }
}
