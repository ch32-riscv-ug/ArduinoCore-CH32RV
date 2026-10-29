/* USB MIDI 1.0 on the device stack, with EspUsbDevice's multi-cable descriptor. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>


static uint8_t clamp_cables(uint8_t n)
{
    return n < 1 ? 1 : n > CH32RVUsbDeviceMidi::MAX_CABLES ? CH32RVUsbDeviceMidi::MAX_CABLES : n;
}

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
    const uint8_t both = _inCables < _outCables ? _inCables : _outCables;
    const uint8_t surplus = (uint8_t)((_inCables > _outCables ? _inCables : _outCables) - both);
    return (uint16_t)(MIDI_HEAD_LEN + MIDI_JACK_LEN * both + MIDI_ONE_WAY_JACK_LEN * surplus +
                      MIDI_EP_LEN(_outCables) + MIDI_EP_LEN(_inCables));
}

/* Audio control + MIDI streaming (two interfaces), OUT and IN on endpoint n.
 * The bulk IN endpoint lists the embedded OUT jacks and the bulk OUT one the
 * embedded IN jacks - the class document names jacks from the device's side. */
static bool midi_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                            uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceMidi &m = const_cast<CH32RVUsbDeviceMidi &>(static_cast<const CH32RVUsbDeviceMidi &>(self));
    const uint8_t in = m.inCableCount(), out = m.outCableCount();
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
    : CH32RVUsbDeviceFunction(device, midi_type, CH32RV_USB_MIDI_NAME), _inCables(clamp_cables(cableCount)),
      _outCables(clamp_cables(cableCount))
{
}

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, CH32RV_USB_MIDI_NAME), _inCables(clamp_cables(inCableCount)),
      _outCables(clamp_cables(outCableCount))
{
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceMidi::usbReset()
{
    _outBusy = _inBusy = false;
}

void CH32RVUsbDeviceMidi::usbConfigured(CH32RVUsbSpeed speed)
{
    _packet = ch32rv_usb_bulk_size(speed);
    if (_packet > sizeof _out) {
        _packet = sizeof _out;
    }
    _rxHead = _rxTail = _txHead = _txTail = 0;
    receive();
}

/* The next OUT transfer only while every packet it may carry fits. */
void CH32RVUsbDeviceMidi::receive()
{
    const uint16_t used = (uint16_t)(_rxHead - _rxTail);
    if (_outBusy || !device().ready() || (uint16_t)(RX_PACKETS - used) * 4u < _packet) {
        return;
    }
    _outBusy = device().transfer(_ep, _out, _packet);
}

void CH32RVUsbDeviceMidi::push()
{
    if (_inBusy || !device().ready() || _txHead == _txTail) {
        return;
    }
    uint16_t n = 0;
    while (_txTail != _txHead && n + 4u <= _packet && n + 4u <= sizeof _in) {
        memcpy(_in + n, _tx[_txTail % TX_PACKETS], 4);
        _txTail++;
        n = (uint16_t)(n + 4);
    }
    _inBusy = device().transfer((uint8_t)(0x80 | _ep), _in, n);
}

void CH32RVUsbDeviceMidi::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _ep) {
        _outBusy = false;
        for (uint16_t i = 0; i + 4u <= length; i += 4) {
            if (_out[i] == 0) {
                continue;                            /* padding */
            }
            memcpy(_rx[_rxHead % RX_PACKETS], _out + i, 4);
            _rxHead++;
        }
        receive();
    } else if (address == (uint8_t)(0x80 | _ep)) {
        _inBusy = false;
        push();
    }
}

/* ---------------------------------------------------------- the API */
bool CH32RVUsbDeviceMidi::readPacket(CH32RVUsbDeviceMidiPacket &packet)
{
    ch32rv_usb_task();
    if (_rxHead == _rxTail) {
        return false;
    }
    memcpy(&packet, _rx[_rxTail % RX_PACKETS], 4);
    _rxTail++;
    receive();
    return true;
}

bool CH32RVUsbDeviceMidi::writePacket(const CH32RVUsbDeviceMidiPacket &packet)
{
    ch32rv_usb_task();
    if (!device().ready() || (uint16_t)(_txHead - _txTail) >= TX_PACKETS) {
        return false;
    }
    memcpy(_tx[_txHead % TX_PACKETS], &packet, 4);
    _txHead++;
    push();
    return true;
}

uint32_t CH32RVUsbDeviceMidi::available()
{
    ch32rv_usb_task();
    return (uint16_t)(_rxHead - _rxTail);
}

/* Channel messages (EspUsbDevice): header = cable << 4 | code index, status =
 * code index << 4 | channel, data clamped to 7 bits. */
bool CH32RVUsbDeviceMidi::send(uint8_t cin, uint8_t channel, uint8_t a, uint8_t b, uint8_t cable)
{
    if (cable >= _inCables) {
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
