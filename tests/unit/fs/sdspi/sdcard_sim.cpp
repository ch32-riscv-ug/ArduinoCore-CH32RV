#include "sdcard_sim.h"

static uint8_t crc7(const uint8_t *p, uint8_t n)
{
    uint8_t crc = 0;
    while (n--) {
        uint8_t d = *p++;
        for (uint8_t i = 0; i < 8; i++) {
            crc <<= 1;
            if ((d ^ crc) & 0x80) {
                crc ^= 0x09;
            }
            d <<= 1;
        }
    }
    return (uint8_t)((crc << 1) | 1);
}

static uint16_t crc16(const uint8_t *p, size_t n)
{
    uint16_t crc = 0;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint8_t SdCardSim::transfer(uint8_t in)
{
    if (!_selected) {
        return 0xFF;
    }
    uint8_t out = 0xFF;
    if (_outPos < _out.size()) {
        out = _out[_outPos++];
        if (_outPos == _out.size()) {
            _out.clear();
            _outPos = 0;
        }
    } else if (_busy > 0) {
        _busy--;
        out = 0x00;
    } else if (_mode == Mode::Busy) {
        _mode = Mode::Idle;
    }
    if (_mode == Mode::ReadStream && _out.empty() && _next < _blocks) {
        queueBlock(_next++);
    }

    if (_mode == Mode::WriteSingle || _mode == Mode::WriteMulti) {
        if (_busy > 0 || !_out.empty()) {
            return out;   /* the card answers or programs; the host only clocks */
        }
        if (!_inData) {
            if (in == 0xFE || in == 0xFC) {
                _inData = true;
                _dataLen = 0;
            } else if (in == 0xFD && _mode == Mode::WriteMulti) {
                _busy = 4;
                _mode = Mode::Busy;
            }
            return out;
        }
        _data[_dataLen++] = in;
        if (_dataLen == 514) {
            _inData = false;
            const uint16_t got = (uint16_t)((_data[512] << 8) | _data[513]);
            if (crcOn && got != crc16(_data, 512)) {
                crcErrors++;
                push(0x0B);
            } else if (refuseWrites || _next >= _blocks) {
                push(0x0D);
            } else {
                memcpy(_mem + (size_t)_next * 512u, _data, 512);
                _next++;
                push(0x05);
            }
            _busy = 5;
            if (_mode == Mode::WriteSingle) {
                _mode = Mode::Busy;
            }
        }
        return out;
    }

    if (_cmdLen == 0) {
        if ((in & 0xC0) == 0x40) {
            _cmd[_cmdLen++] = in;
        }
    } else {
        _cmd[_cmdLen++] = in;
        if (_cmdLen == 6) {
            _cmdLen = 0;
            command();
        }
    }
    return out;
}

void SdCardSim::queueBlock(uint32_t block)
{
    push(0xFF);
    push(0xFF);
    push(0xFE);
    const uint8_t *p = _mem + (size_t)block * 512u;
    for (int i = 0; i < 512; i++) {
        push(p[i]);
    }
    uint16_t crc = crc16(p, 512);
    if (breakReadCrcOnce > 0 && --breakReadCrcOnce == 0) {
        crc ^= 1;
    }
    push((uint8_t)(crc >> 8));
    push((uint8_t)crc);
}

void SdCardSim::command()
{
    const uint8_t index = _cmd[0] & 0x3F;
    const uint32_t arg = ((uint32_t)_cmd[1] << 24) | ((uint32_t)_cmd[2] << 16) | ((uint32_t)_cmd[3] << 8) | _cmd[4];
    commands++;
    const bool app = _app;
    _app = false;
    if (_mode == Mode::ReadStream && index != 12) {
        return;   /* only CMD12 ends a stream */
    }
    _out.clear();
    _outPos = 0;
    push(0xFF);   /* NCR: the answer one byte later */
    if ((crcOn || index == 0 || index == 8) && crc7(_cmd, 5) != _cmd[5]) {
        crcErrors++;
        push(0x08 | (_idle ? 1 : 0));
        return;
    }
    const uint8_t r1 = _idle ? 0x01 : 0x00;
    const bool v2 = _kind != SimKind::SdscV1;
    if (app && index == 41) {
        if (_kind == SimKind::Sdhc && !(arg & 0x40000000u)) {
            push(0x01);   /* an SDHC card never leaves idle without HCS */
            return;
        }
        if (++_initPolls >= 3) {
            _idle = false;
            _ready = true;
        }
        push(_idle ? 0x01 : 0x00);
        return;
    }
    switch (index) {
    case 0:
        _idle = true;
        _ready = false;
        _initPolls = 0;
        _mode = Mode::Idle;
        push(0x01);
        break;
    case 59:
        crcOn = arg & 1;
        push(r1);
        break;
    case 8:
        if (!v2) {
            push(0x05);
            break;
        }
        push(0x01);
        push(0);
        push(0);
        push((uint8_t)(arg >> 8));
        push((uint8_t)arg);
        break;
    case 55:
        _app = true;
        push(r1);
        break;
    case 58:
        push(r1);
        push((uint8_t)((_ready ? 0x80 : 0) | (_ready && _kind == SimKind::Sdhc ? 0x40 : 0)));
        push(0xFF);
        push(0x80);
        push(0x00);
        break;
    case 9: {
        push(r1);
        uint8_t csd[16] = {};
        if (_kind == SimKind::Sdhc) {
            const uint32_t c = _blocks / 1024u - 1u;
            csd[0] = 0x40;
            csd[7] = (uint8_t)((c >> 16) & 0x3F);
            csd[8] = (uint8_t)(c >> 8);
            csd[9] = (uint8_t)c;
        } else {
            /* READ_BL_LEN 9, C_SIZE_MULT 7: blocks = (C_SIZE + 1) * 512 */
            const uint32_t c = _blocks / 512u - 1u;
            csd[5] = 9;
            csd[6] = (uint8_t)((c >> 10) & 3);
            csd[7] = (uint8_t)(c >> 2);
            csd[8] = (uint8_t)((c & 3) << 6);
            csd[9] = 0x03;   /* C_SIZE_MULT bits 2-1 */
            csd[10] = 0x80;  /* C_SIZE_MULT bit 0 */
        }
        csd[15] = (uint8_t)((crc7(csd, 15) & 0xFE) | 1);
        push(0xFF);
        push(0xFE);
        for (uint8_t b : csd) {
            push(b);
        }
        const uint16_t crc = crc16(csd, 16);
        push((uint8_t)(crc >> 8));
        push((uint8_t)crc);
        break;
    }
    case 16:
        push(arg == 512 ? r1 : (uint8_t)(r1 | 0x40));
        break;
    case 13:
        push(r1);
        push(0x00);
        break;
    case 17:
    case 18:
    case 24:
    case 25: {
        const uint32_t block = blockOf(arg);
        if (_idle || block >= _blocks || (_kind != SimKind::Sdhc && arg % 512u)) {
            push((uint8_t)(r1 | 0x20));   /* address error */
            break;
        }
        push(0x00);
        _next = block;
        if (index == 17) {
            queueBlock(_next++);
        } else if (index == 18) {
            multiReads++;
            _mode = Mode::ReadStream;
        } else {
            if (index == 25) {
                multiWrites++;
            }
            _mode = index == 24 ? Mode::WriteSingle : Mode::WriteMulti;
            _inData = false;
        }
        break;
    }
    case 12:
        _mode = Mode::Busy;
        _out.clear();
        _outPos = 0;
        push(0xFF);   /* the stuff byte */
        push(0x00);
        _busy = 3;
        break;
    default:
        push((uint8_t)(r1 | 0x04));
        break;
    }
}
