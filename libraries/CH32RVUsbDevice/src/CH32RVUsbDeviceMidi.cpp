/* USB MIDI 1.0 on the device stack, with EspUsbDevice's multi-cable descriptor. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

/* Jack IDs: four per cable, numbered from the device's side - embedded IN,
 * external IN, embedded OUT, external OUT (as TinyUSB and EspUsbDevice). A
 * cable in both directions has all four jacks; a one-way cable the pair its
 * direction needs. */
#define JACK_IN_EMB(c)  (uint8_t)(((c) - 1) * 4 + 1)
#define JACK_IN_EXT(c)  (uint8_t)(((c) - 1) * 4 + 2)
#define JACK_OUT_EMB(c) (uint8_t)(((c) - 1) * 4 + 3)
#define JACK_OUT_EXT(c) (uint8_t)(((c) - 1) * 4 + 4)
#define MIDI_ONE_WAY_JACK_LEN (6 + 9)

#define MIDI_HEAD_LEN   (9 + 9 + 9 + 7)
#define MIDI_JACK_LEN   (6 + 6 + 9 + 9)
#define MIDI_EP_LEN(n)  (9 + 4 + (n))

uint16_t CH32RVUsbDeviceMidi::descriptorLength() const
{
    const uint8_t in = _config.inCables, out = _config.outCables;
    const uint8_t both = in < out ? in : out;
    const uint8_t surplus = (uint8_t)((in > out ? in : out) - both);
    return (uint16_t)(MIDI_HEAD_LEN + MIDI_JACK_LEN * both + MIDI_ONE_WAY_JACK_LEN * surplus + MIDI_EP_LEN(out) +
                      MIDI_EP_LEN(in));
}

/* Audio control + MIDI streaming (two interfaces), OUT and IN on endpoint n.
 * The bulk IN endpoint lists the embedded OUT jacks and the bulk OUT one the
 * embedded IN jacks - the class document names jacks from the device's side. */
static bool midi_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                            uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceMidi &m = const_cast<CH32RVUsbDeviceMidi &>(static_cast<const CH32RVUsbDeviceMidi &>(self));
    const uint8_t in = m.inCableCount(), out = m.outCableCount();
    if (!in || in > CH32RVUsbDeviceMidi::MAX_CABLES || !out || out > CH32RVUsbDeviceMidi::MAX_CABLES ||
        !m.receiveReady()) {
        return self.refuseSettings();
    }
    const uint8_t both = in < out ? in : out;
    const uint8_t n = (*ep)++;
    const uint16_t mps = ch32rv_usb_bulk_size(speed);
    m._ep = n;
    /* wTotalLength of the MS header covers it and everything after it. */
    const uint16_t ms_total = (uint16_t)(m.descriptorLength() - (MIDI_HEAD_LEN - 7));
    {
        const uint8_t head[] = {
            9, 4, itf, 0, 0, 1, 1, 0, str,                            /* audio control */
            9, 0x24, 1, 0x00, 0x01, 9, 0, 1, (uint8_t)(itf + 1),      /* AC header */
            9, 4, (uint8_t)(itf + 1), 0, 2, 1, 3, 0, 0,               /* MIDI streaming */
            7, 0x24, 1, 0x00, 0x01, (uint8_t)ms_total, (uint8_t)(ms_total >> 8),   /* MS header */
        };
        w.put(head, sizeof head);
    }
    for (uint8_t c = 1; c <= both; c++) {
        const uint8_t jack[] = {
            6, 0x24, 2, 1, JACK_IN_EMB(c), 0,
            6, 0x24, 2, 2, JACK_IN_EXT(c), 0,
            9, 0x24, 3, 1, JACK_OUT_EMB(c), 1, JACK_IN_EXT(c), 1, 0,
            9, 0x24, 3, 2, JACK_OUT_EXT(c), 1, JACK_IN_EMB(c), 1, 0,
        };
        w.put(jack, sizeof jack);
    }
    for (uint8_t c = (uint8_t)(both + 1); c <= in; c++) {
        const uint8_t jack[] = {
            6, 0x24, 2, 2, JACK_IN_EXT(c), 0,
            9, 0x24, 3, 1, JACK_OUT_EMB(c), 1, JACK_IN_EXT(c), 1, 0,
        };
        w.put(jack, sizeof jack);
    }
    for (uint8_t c = (uint8_t)(both + 1); c <= out; c++) {
        const uint8_t jack[] = {
            6, 0x24, 2, 1, JACK_IN_EMB(c), 0,
            9, 0x24, 3, 2, JACK_OUT_EXT(c), 1, JACK_IN_EMB(c), 1, 0,
        };
        w.put(jack, sizeof jack);
    }
    {
        /* Audio 1.0 endpoints are 9 bytes; then the MS endpoint with its jacks. */
        const uint8_t e[] = {9, 5, n, 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0, 0, 0,
                             (uint8_t)(4 + out), 0x25, 1, out};
        w.put(e, sizeof e);
    }
    for (uint8_t c = 1; c <= out; c++) {
        w.u8(JACK_IN_EMB(c));
    }
    {
        const uint8_t e[] = {9, 5, (uint8_t)(0x80 | n), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0, 0, 0,
                             (uint8_t)(4 + in), 0x25, 1, in};
        w.put(e, sizeof e);
    }
    for (uint8_t c = 1; c <= in; c++) {
        w.u8(JACK_OUT_EMB(c));
    }
    return true;
}

static const CH32RVUsbFunctionType midi_type = {midi_descriptor, 2, 4, nullptr, 0};

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t cableCount)
    : CH32RVUsbDeviceMidi(device, cableCount, cableCount)
{
}

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, CH32RV_USB_MIDI_NAME)
{
    _config.inCables = inCableCount;
    _config.outCables = outCableCount;
}

static bool midi_queue(CH32RVUsbDeviceMidiPacket *buffer, size_t packets, CH32RVUsbDeviceMidiPacket *store,
                       uint16_t storeSize, CH32RVUsbDeviceMidiPacket *&to, uint16_t &size)
{
    if (!buffer) {
        packets = packets ? packets : storeSize;
        if (packets > storeSize) {
            return false;
        }
        buffer = store;
    }
    if (!packets || packets > 65535u) {
        return false;
    }
    to = buffer;
    size = (uint16_t)packets;
    return true;
}

bool CH32RVUsbDeviceMidi::configure(const CH32RVUsbMidiConfig &config)
{
    CH32RVUsbDeviceMidiPacket *rx, *tx;
    uint16_t rxSize, txSize;
    if (device().started() || !config.inCables || config.inCables > MAX_CABLES || !config.outCables ||
        config.outCables > MAX_CABLES || config.rxMode > CH32RVUsbMidiRxMode::Disabled ||
        config.streamTermination > CH32RVUsbStreamTermination::EndWithShortPacket ||
        !midi_queue(config.rxBuffer, config.rxPackets, _rxStore, RX_PACKETS, rx, rxSize) ||
        !midi_queue(config.txBuffer, config.txPackets, _txStore, TX_PACKETS, tx, txSize)) {
        return false;
    }
    _config = config;
    _rx = rx;
    _rxSize = rxSize;
    _tx = tx;
    _txSize = txSize;
    if (config.name) {
        setName(config.name);
    }
    _rxTail = _rxUsed = _txTail = _txUsed = 0;
    return true;
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceMidi::usbReset()
{
    _outBusy = _inBusy = _zlp = false;
}

void CH32RVUsbDeviceMidi::usbConfigured(CH32RVUsbSpeed speed)
{
    _packet = ch32rv_usb_bulk_size(speed);
    if (_packet > sizeof _out) {
        _packet = sizeof _out;
    }
    _rxTail = _rxUsed = _txTail = _txUsed = 0;
    _outLength = _outAt = 0;
    receive();
}

/* The next OUT packet once the last one has all gone to the queue. */
void CH32RVUsbDeviceMidi::receive()
{
    if (_outBusy || !device().ready() || _config.rxMode == CH32RVUsbMidiRxMode::Disabled || _outAt < _outLength) {
        return;
    }
    _outLength = _outAt = 0;
    _outBusy = device().transfer(_ep, _out, _packet);
}

/* Move events from the OUT packet to the queue while it has room; a
 * reserved code index (0, 1) or a cable beyond outCableCount() is dropped
 * (all-zero is padding). */
static bool midi_valid(const uint8_t *p, uint8_t cables)
{
    return (p[0] & 0x0f) >= 2 && (p[0] >> 4) < cables;
}

void CH32RVUsbDeviceMidi::drain()
{
    while (_outAt < _outLength && _rxUsed < _rxSize) {
        const uint8_t *p = _out + _outAt;
        _outAt = (uint16_t)(_outAt + 4);
        if (!midi_valid(p, _config.outCables)) {
            if (p[0]) {
                _receiveErrors++;
            }
            continue;
        }
        memcpy(&_rx[(_rxTail + _rxUsed) % _rxSize], p, 4);
        _rxUsed++;
    }
    receive();
}

size_t CH32RVUsbDeviceMidi::waitingInPacket() const
{
    size_t n = 0;
    for (uint16_t i = _outAt; i < _outLength; i = (uint16_t)(i + 4)) {
        n += midi_valid(_out + i, _config.outCables);
    }
    return n;
}

void CH32RVUsbDeviceMidi::push()
{
    if (_inBusy || !device().ready()) {
        return;
    }
    if (!_txUsed) {
        if (_zlp && device().transfer((uint8_t)(0x80 | _ep), _in, 0)) {
            _zlp = false;
            _inBusy = true;
            _inLength = 0;
        }
        return;
    }
    const size_t limit = transferCapacity() * 4;
    uint16_t n = 0;
    while (_txUsed && n + 4u <= limit) {
        memcpy(_in + n, &_tx[_txTail], 4);
        _txTail = (uint16_t)((_txTail + 1) % _txSize);
        _txUsed--;
        n = (uint16_t)(n + 4);
    }
    _inBusy = device().transfer((uint8_t)(0x80 | _ep), _in, n);
    _inLength = n;
}

void CH32RVUsbDeviceMidi::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _ep) {
        _outBusy = false;
        if (length % 4u) {
            _receiveErrors++;
        }
        _outLength = (uint16_t)(length - length % 4u);
        _outAt = 0;
        drain();
    } else if (address == (uint8_t)(0x80 | _ep)) {
        _inBusy = false;
        _zlp = _inLength == _packet && !_txUsed &&
               _config.streamTermination != CH32RVUsbStreamTermination::Continue;
        push();
    }
}

size_t CH32RVUsbDeviceMidi::usbDeliver()
{
    size_t ran = 0;
    while (_config.rxMode == CH32RVUsbMidiRxMode::Callback && _onPacket && _rxUsed) {
        const CH32RVUsbDeviceMidiPacket packet = _rx[_rxTail];
        _rxTail = (uint16_t)((_rxTail + 1) % _rxSize);
        _rxUsed--;
        drain();
        _onPacket(packet);
        ran++;
    }
    return ran;
}

bool CH32RVUsbDeviceMidi::receiveReady() const
{
    return _config.rxMode != CH32RVUsbMidiRxMode::Callback || _onPacket;
}

/* ---------------------------------------------------------- the API */
bool CH32RVUsbDeviceMidi::readPacket(CH32RVUsbDeviceMidiPacket &packet)
{
    ch32rv_usb_task();
    if (!_rxUsed) {
        return false;
    }
    packet = _rx[_rxTail];
    _rxTail = (uint16_t)((_rxTail + 1) % _rxSize);
    _rxUsed--;
    drain();
    return true;
}

bool CH32RVUsbDeviceMidi::writePacket(const CH32RVUsbDeviceMidiPacket &packet)
{
    if ((packet.header & 0x0f) < 2 || (packet.header >> 4) >= _config.inCables) {
        return false;
    }
    ch32rv_usb_task();
    if (!device().ready() || _txUsed >= _txSize) {
        return false;
    }
    _tx[(_txTail + _txUsed) % _txSize] = packet;
    _txUsed++;
    if (_config.autoFlush) {
        push();
    }
    return true;
}

void CH32RVUsbDeviceMidi::flush()
{
    push();
    ch32rv_usb_task();
}

size_t CH32RVUsbDeviceMidi::availablePackets()
{
    ch32rv_usb_task();
    return _rxUsed + waitingInPacket();
}

size_t CH32RVUsbDeviceMidi::availableForWritePackets() const
{
    return device().ready() ? (size_t)(_txSize - _txUsed) : 0;
}

size_t CH32RVUsbDeviceMidi::receiveCapacity() const
{
    return device().started() ? _rxSize : 0;
}

size_t CH32RVUsbDeviceMidi::sendCapacity() const
{
    return device().started() ? _txSize : 0;
}

size_t CH32RVUsbDeviceMidi::transferCapacity() const
{
    if (!device().started()) {
        return 0;
    }
    size_t n = (_packet < sizeof _in ? _packet : sizeof _in) / 4;
    if (_config.transferPackets && _config.transferPackets < n) {
        n = _config.transferPackets;
    }
    return n;
}

/* Channel messages (EspUsbDevice): header = cable << 4 | code index, status =
 * code index << 4 | channel, data clamped to 7 bits. */
bool CH32RVUsbDeviceMidi::send(uint8_t cin, uint8_t channel, uint8_t a, uint8_t b, uint8_t cable)
{
    if (cable >= _config.inCables) {
        return false;
    }
    if (channel > 15) {
        channel = 15;
    }
    CH32RVUsbDeviceMidiPacket packet;
    packet.header = (uint8_t)((cable << 4) | (cin & 0x0f));
    packet.byte1 = (uint8_t)((cin << 4) | channel);
    packet.byte2 = a > 127 ? 127 : a;
    packet.byte3 = b > 127 ? 127 : b;
    return writePacket(packet);
}

bool CH32RVUsbDeviceMidi::noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_NOTE_ON, channel, note, velocity, cable);
}

bool CH32RVUsbDeviceMidi::noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_NOTE_OFF, channel, note, velocity, cable);
}

bool CH32RVUsbDeviceMidi::controlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE, channel, control, value, cable);
}

bool CH32RVUsbDeviceMidi::programChange(uint8_t channel, uint8_t program, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE, channel, program, 0, cable);
}

bool CH32RVUsbDeviceMidi::polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS, channel, note, pressure, cable);
}

bool CH32RVUsbDeviceMidi::channelPressure(uint8_t channel, uint8_t pressure, uint8_t cable)
{
    return send(CH32RV_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE, channel, pressure, 0, cable);
}

bool CH32RVUsbDeviceMidi::pitchBend(uint8_t channel, uint16_t value, uint8_t cable)
{
    if (value > 16383) {
        value = 16383;
    }
    return send(CH32RV_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE, channel, (uint8_t)(value & 0x7f),
                (uint8_t)((value >> 7) & 0x7f), cable);
}
