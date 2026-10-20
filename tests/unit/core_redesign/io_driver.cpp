#include "io_lifecycle.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace experiment;
using Slot = IoSlot<32>;
void generation() {
    IoSlot<32, uint8_t> slot;
    IoBuffer<32> buffer;
    static_assert(!std::is_copy_constructible<Slot>::value && !std::is_move_constructible<Slot>::value);
    assert(slot.open());
    for (unsigned i = 1; i <= 255; ++i) {
        assert(slot.start(&buffer, 1, IoMode::Write, 1) == IoStart::Accepted);
        assert(slot.token() == i);
        assert(slot.hardware_terminal(uint8_t(i), IoResult::Success, 1));
        IoSlot<32, uint8_t>::Completion value;
        assert(slot.take(uint8_t(i), value));
    }
    assert(slot.start(&buffer, 1, IoMode::Write, 1) == IoStart::Exhausted);
    assert(slot.end() && slot.open());
    assert(slot.start(&buffer, 1, IoMode::Write, 1) == IoStart::Exhausted);
    assert(buffer.available());
}

int main(int argc, char** argv) {
    if (argc == 2) { assert(std::string(argv[1]) == "generation"); generation(); return 0; }
    assert(argc == 1);
    Slot slots[2]; IoBuffer<32> buffers[2];
    Slot::Completion last;
    uint8_t read = 99;
    Slot::Token notice = 999;
    std::string command;
    while (std::cin >> command) {
        unsigned s; std::cin >> s;
        int result = -1;
        if (command == "write") {
            unsigned offset, value; std::cin >> offset >> value; assert(s < 2);
            result = buffers[s].write(offset, uint8_t(value));
        } else if (command == "read") {
            unsigned offset; std::cin >> offset; assert(s < 2);
            result = buffers[s].read(offset, read);
        } else {
            assert(s < 2); auto& slot = slots[s];
            if (command == "open") result = slot.open();
            else if (command == "end") result = slot.end();
            else if (command == "start") {
                int b; unsigned size, mode, address; std::cin >> b >> size >> mode >> address;
                assert(b >= -1 && b < 2);
                result = int(slot.start(b < 0 ? nullptr : &buffers[b], size, IoMode(mode), address));
            } else if (command == "peek") result = slot.peek_notice(notice);
            else {
                unsigned token; std::cin >> token;
                if (command == "cancel") result = int(slot.cancel(token));
                else if (command == "ack") result = slot.ack_notice(token);
                else if (command == "take") result = slot.take(token, last);
                else {
                    assert(command == "finish"); unsigned outcome, count, fill; std::cin >> outcome >> count >> fill;
                    result = slot.hardware_terminal(token, IoResult(outcome), count, uint8_t(fill));
                }
            }
        }
        std::cout << result << ' ' << unsigned(read) << ' ' << notice << ' '
                  << int(last.result) << ' ' << last.transferred << ' ' << last.digest;
        for (const auto& slot : slots)
            std::cout << ' ' << slot.opened() << ' ' << slot.active() << ' ' << slot.terminal()
                      << ' ' << slot.cancel_pending() << ' ' << slot.notification_pending()
                      << ' ' << slot.token() << ' ' << int(slot.result());
        for (const auto& buffer : buffers) {
            uint32_t digest = 2166136261u;
            if (buffer.available()) for (unsigned i = 0; i < 32; ++i) {
                uint8_t value = 0; assert(buffer.read(i, value)); digest = (digest ^ value) * 16777619u;
            }
            std::cout << ' ' << buffer.available() << ' ' << (buffer.available() ? digest : 0);
        }
        std::cout << '\n';
    }
}
