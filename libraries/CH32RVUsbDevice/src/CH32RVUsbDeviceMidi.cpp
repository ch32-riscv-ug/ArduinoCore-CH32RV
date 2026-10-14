/* USB MIDI: TinyUSB's midi_device, with EspUsbDevice's multi-cable descriptor. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

static uint8_t clamp_cables(uint8_t n)
{
    return n < 1 ? 1 : n > CH32RVUsbDeviceMidi::MAX_CABLES ? CH32RVUsbDeviceMidi::MAX_CABLES : n;
}

/* TUD_MIDI_DESC_JACK_DESC() emits all four jacks of a cable, i.e. a cable in
 * both directions. These emit the pair a one-way cable needs, with the same
 * jack ID formula (EspUsbDevice). */
#define MIDI_ONE_WAY_JACK_LEN (6 + 9)
#define MIDI_DESC_JACK_IN_ONLY(_cablenum) \
    6, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_IN_JACK, MIDI_JACK_EXTERNAL, TUD_MIDI_JACKID_IN_EXT(_cablenum), 0, \
    9, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_OUT_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_OUT_EMB(_cablenum), 1, TUD_MIDI_JACKID_IN_EXT(_cablenum), 1, 0
#define MIDI_DESC_JACK_OUT_ONLY(_cablenum) \
    6, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_IN_JACK, MIDI_JACK_EMBEDDED, TUD_MIDI_JACKID_IN_EMB(_cablenum), 0, \
    9, TUSB_DESC_CS_INTERFACE, MIDI_CS_INTERFACE_OUT_JACK, MIDI_JACK_EXTERNAL, TUD_MIDI_JACKID_OUT_EXT(_cablenum), 1, TUD_MIDI_JACKID_IN_EMB(_cablenum), 1, 0

/* TinyUSB's TUD_MIDI_DESC_HEAD_LEN, _JACK_LEN and _EP_LEN(n), here because
 * usbd.h does not define them on a series without the device stack. */
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

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t midi_driver = {
    NULL, midid_init, midid_deinit, midid_reset, midid_open, midid_control_xfer_cb, midid_xfer_cb, NULL, NULL,
};

/* Audio control + MIDI streaming (two interfaces), OUT and IN on endpoint n.
 * The bulk IN endpoint lists the embedded OUT jacks and the bulk OUT one the
 * embedded IN jacks - the class document names jacks from the device's side. */
static uint16_t midi_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *dst, uint16_t room,
                                uint8_t itf, uint8_t *ep, uint8_t str)
{
    const CH32RVUsbDeviceMidi &m = static_cast<const CH32RVUsbDeviceMidi &>(self);
    if (m.descriptorLength() > room) {
        return 0;
    }
    const uint8_t in = m.inCableCount(), out = m.outCableCount();
    const uint8_t both = in < out ? in : out;
    const uint8_t n = (*ep)++;
    uint16_t at = 0;
    {
        const uint8_t head[] = {TUD_MIDI_DESC_HEAD(itf, str, in)};
        memcpy(dst + at, head, sizeof head);
        at = (uint16_t)(at + sizeof head);
    }
    for (uint8_t cable = 1; cable <= both; cable++) {
        const uint8_t jack[] = {TUD_MIDI_DESC_JACK_DESC(cable, 0)};
        memcpy(dst + at, jack, sizeof jack);
        at = (uint16_t)(at + sizeof jack);
    }
    for (uint8_t cable = (uint8_t)(both + 1); cable <= in; cable++) {
        const uint8_t jack[] = {MIDI_DESC_JACK_IN_ONLY(cable)};
        memcpy(dst + at, jack, sizeof jack);
        at = (uint16_t)(at + sizeof jack);
    }
    for (uint8_t cable = (uint8_t)(both + 1); cable <= out; cable++) {
        const uint8_t jack[] = {MIDI_DESC_JACK_OUT_ONLY(cable)};
        memcpy(dst + at, jack, sizeof jack);
        at = (uint16_t)(at + sizeof jack);
    }
    {
        const uint8_t e[] = {TUD_MIDI_DESC_EP(n, 64, out)};
        memcpy(dst + at, e, sizeof e);
        at = (uint16_t)(at + sizeof e);
    }
    for (uint8_t cable = 1; cable <= out; cable++) {
        dst[at++] = TUD_MIDI_JACKID_IN_EMB(cable);
    }
    {
        const uint8_t e[] = {TUD_MIDI_DESC_EP((uint8_t)(0x80 | n), 64, in)};
        memcpy(dst + at, e, sizeof e);
        at = (uint16_t)(at + sizeof e);
    }
    for (uint8_t cable = 1; cable <= in; cable++) {
        dst[at++] = TUD_MIDI_JACKID_OUT_EMB(cable);
    }
    /* wTotalLength of the MS header covers it and everything after it. */
    const uint16_t ms_total = (uint16_t)(at - (TUD_MIDI_DESC_HEAD_LEN - 7));
    dst[TUD_MIDI_DESC_HEAD_LEN - 2] = (uint8_t)(ms_total & 0xff);
    dst[TUD_MIDI_DESC_HEAD_LEN - 1] = (uint8_t)(ms_total >> 8);
    return at;
}

static const CH32RVUsbFunctionType midi_type = {&midi_driver, midi_descriptor, 2, CFG_TUD_MIDI};

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t cableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI"), _inCables(clamp_cables(cableCount)),
      _outCables(clamp_cables(cableCount))
{
}

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI"), _inCables(clamp_cables(inCableCount)),
      _outCables(clamp_cables(outCableCount))
{
}

bool CH32RVUsbDeviceMidi::readPacket(CH32RVUsbDeviceMidiPacket &packet)
{
    ch32rv_usb_task();
    return tud_midi_n_packet_read(instance(), reinterpret_cast<uint8_t *>(&packet));
}

bool CH32RVUsbDeviceMidi::writePacket(const CH32RVUsbDeviceMidiPacket &packet)
{
    ch32rv_usb_task();
    return tud_midi_n_mounted(instance()) &&
           tud_midi_n_packet_write(instance(), reinterpret_cast<const uint8_t *>(&packet));
}

uint32_t CH32RVUsbDeviceMidi::available()
{
    ch32rv_usb_task();
    return tud_midi_n_available(instance(), 0) / 4u;
}

#else

static uint16_t midi_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType midi_type = {nullptr, midi_descriptor, 2, 1};

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t cableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI"), _inCables(clamp_cables(cableCount)),
      _outCables(clamp_cables(cableCount))
{
}

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI"), _inCables(clamp_cables(inCableCount)),
      _outCables(clamp_cables(outCableCount))
{
}

bool CH32RVUsbDeviceMidi::readPacket(CH32RVUsbDeviceMidiPacket &) { return false; }
bool CH32RVUsbDeviceMidi::writePacket(const CH32RVUsbDeviceMidiPacket &) { return false; }
uint32_t CH32RVUsbDeviceMidi::available() { return 0; }

#endif

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
