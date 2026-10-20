#include "acquisition_buffers.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
using namespace acquisition_experiment;

template<unsigned N, class G> int run() {
    using Pool = Buffers<N, G>;
    Pool pool;
    typename Pool::Token token{G(0), 99};
    typename Pool::Completion completion{{G(0), 99}, 999, 999, Result::Failed};
    uint16_t output = 54321;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        unsigned a=0, b=0, c=0, d=0, e=0;
        in >> op >> a >> b >> c >> d >> e;
        typename Pool::Token argument{G(a), uint8_t(b)};
        int status = 0;
        if (op == "open") status = pool.open();
        else if (op == "end") pool.end();
        else if (op == "reserve") status = int(pool.reserve(a, token));
        else if (op == "write") status = pool.write(argument, c, uint16_t(d));
        else if (op == "cancel") status = pool.cancel(argument);
        else if (op == "finish") status = pool.hardware_terminal(argument, Result(c), d, e != 0);
        else if (op == "notice") status = pool.peek_notice(token);
        else if (op == "ack") status = pool.ack_notice(argument);
        else if (op == "take") status = pool.take(completion);
        else if (op == "sample") status = pool.sample(argument, c, output);
        else if (op == "release") status = pool.release(argument);
        else return 2;
        std::cout << "{\"status\":" << status << ",\"token\":[" << unsigned(token.generation) << ',' << unsigned(token.index)
                  << "],\"completion\":[" << unsigned(completion.token.generation) << ',' << unsigned(completion.token.index)
                  << ',' << completion.capture << ',' << completion.count << ',' << unsigned(completion.result)
                  << "],\"sample\":" << output << ",\"blocks\":[";
        for (unsigned i=0; i<2; ++i) {
            auto s = pool.snapshot(i);
            if (i) std::cout << ',';
            std::cout << '[' << unsigned(s.generation) << ',' << s.capture << ',' << s.written << ',' << s.count
                      << ',' << unsigned(s.phase) << ',' << unsigned(s.result) << ',' << s.cancelled << ',' << s.notice << ",[";
            for (unsigned j=0; j<N; ++j) {
                uint16_t value = 0;
                if (j) std::cout << ',';
                if (pool.sample({s.generation, uint8_t(i)}, j, value)) std::cout << value;
                else std::cout << -1;
            }
            std::cout << "]]";
        }
        std::cout << "]}\n";
    }
    static_assert(!std::is_copy_constructible<Pool>::value && !std::is_move_constructible<Pool>::value, "stable lifetime");
    return 0;
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const bool small = std::string(argv[2]) == "short";
    switch (std::stoi(argv[1])) {
    case 8: return small ? run<8, uint8_t>() : run<8, uint32_t>();
    case 64: return small ? run<64, uint8_t>() : run<64, uint32_t>();
    case 128: return small ? run<128, uint8_t>() : run<128, uint32_t>();
    default: return 2;
    }
}
