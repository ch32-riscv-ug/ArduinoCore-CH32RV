/* An SD card in SPI mode, on the host, byte by byte as the bus sees it
 * (SD Physical Layer Simplified Specification 9.00, chapter 7), over a disk
 * image: CMD0 / 8 / 55+41 / 58 / 59 / 9 / 16 / 17 / 18 / 12 / 24 / 25 / 13,
 * the command CRC7 always checked for CMD0 and CMD8 and for all with CMD59,
 * the data CRC16 likewise. A card that is SDHC (block addresses) or SDSC
 * (byte addresses, v2 or v1). Faults on request: a read block's CRC broken,
 * a write refused. */
#pragma once
#include "SPI.h"
#include <stdint.h>
#include <string.h>
#include <vector>

enum class SimKind { Sdhc, SdscV2, SdscV1 };

class SdCardSim : public SPIClass {
public:
    SdCardSim(uint8_t *mem, uint32_t blocks, SimKind kind) : _mem(mem), _blocks(blocks), _kind(kind) {}
    uint8_t transfer(uint8_t in) override;
    void beginTransaction(arduino::SPISettings s) override { lastClock = s.getClockFreq(); }
    void endTransaction() override {}
    void select(bool low)
    {
        if (!low) {
            _cmdLen = 0;
            if (_mode != Mode::Idle && _mode != Mode::Busy) {
                _mode = Mode::Idle;   /* CS high ends what was going on */
                _out.clear();
            }
        }
        _selected = low;
    }

    uint32_t lastClock = 0;
    int breakReadCrcOnce = 0;   /* > 0: the n-th block read next goes out with a bad CRC */
    bool refuseWrites = false;
    uint32_t commands = 0, multiReads = 0, multiWrites = 0, crcErrors = 0;
    bool crcOn = false;

private:
    enum class Mode { Idle, ReadStream, WriteSingle, WriteMulti, Busy };
    void command();
    void queueBlock(uint32_t block);
    void push(uint8_t b) { _out.push_back(b); }
    uint32_t blockOf(uint32_t arg) const { return _kind == SimKind::Sdhc ? arg : arg / 512u; }

    uint8_t *_mem;
    uint32_t _blocks;
    SimKind _kind;
    bool _selected = false, _idle = true, _app = false, _ready = false;
    int _initPolls = 0;
    uint8_t _cmd[6];
    uint8_t _cmdLen = 0;
    std::vector<uint8_t> _out;
    size_t _outPos = 0;
    Mode _mode = Mode::Idle;
    uint32_t _next = 0;           /* the next block of a stream */
    uint8_t _data[514];
    uint32_t _dataLen = 0;
    bool _inData = false;
    uint8_t _token = 0;
    int _busy = 0;
};
