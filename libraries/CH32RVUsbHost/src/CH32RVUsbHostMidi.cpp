/* CH32RVUsbHost: USB MIDI 1.0 (TinyUSB's MIDI host driver). Messages are the
 * 4-byte USB-MIDI event packets; onMidiMessage() gets them decoded. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_TINYUSB_HOST && CFG_TUH_MIDI

namespace {

const usbh_class_driver_t midi_driver = {
    "MIDI", midih_init, midih_deinit, midih_open, midih_set_config, midih_xfer_cb, midih_close,
};

struct Port {
    bool mounted;
    uint8_t daddr, itf, rxCables, txCables;
};
Port ports[CFG_TUH_MIDI];
CH32RVUsbHost::MidiMessageCallback message_cb;

void poll(CH32RVUsbHost &host);
CH32RVUsbHostGroup midi_group = {&midi_driver, poll, nullptr, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(midi_group); }
} join;

void poll(CH32RVUsbHost &host)
{
    (void)host;
    if (!message_cb) {
        return;
    }
    for (uint8_t i = 0; i < CFG_TUH_MIDI; i++) {
        if (!ports[i].mounted) {
            continue;
        }
        uint8_t p[4];
        while (tuh_midi_packet_read(i, p)) {
            CH32RVUsbHostMidiMessage m;
            m.address = ports[i].daddr;
            m.interfaceNumber = ports[i].itf;
            m.cable = (uint8_t)(p[0] >> 4);
            m.codeIndex = (uint8_t)(p[0] & 0x0F);
            m.status = p[1];
            m.data1 = p[2];
            m.data2 = p[3];
            m.raw = p;
            m.length = 4;
            message_cb(m);
        }
    }
}

int index_of(uint8_t address)
{
    for (int i = 0; i < CFG_TUH_MIDI; i++) {
        if (ports[i].mounted && (address == CH32RV_USB_HOST_ANY_ADDRESS || ports[i].daddr == address)) {
            return i;
        }
    }
    return -1;
}

/* Code Index Number for a channel message, from its status byte. */
uint8_t cin_of(uint8_t status)
{
    return (uint8_t)(status >> 4);
}

}  // namespace

extern "C" void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *info)
{
    if (idx < CFG_TUH_MIDI) {
        Port &p = ports[idx];
        p.daddr = info->daddr;
        p.itf = info->bInterfaceNumber;
        p.rxCables = info->rx_cable_count;
        p.txCables = info->tx_cable_count;
        p.mounted = true;
        ch32rv_usb_host_mark_claimed(info->daddr, info->bInterfaceNumber);
        if (info->bInterfaceNumber) {
            ch32rv_usb_host_mark_claimed(info->daddr, (uint8_t)(info->bInterfaceNumber - 1u)); /* audio control */
        }
    }
}

extern "C" void tuh_midi_umount_cb(uint8_t idx)
{
    if (idx < CFG_TUH_MIDI) {
        ports[idx].mounted = false;
    }
}

void CH32RVUsbHost::onMidiMessage(MidiMessageCallback cb) { message_cb = cb; }

bool CH32RVUsbHost::midiReady(uint8_t address) const
{
    return index_of(address) >= 0;
}

bool CH32RVUsbHost::getMidiPortInfo(CH32RVUsbHostMidiPortInfo &info, uint8_t address) const
{
    const int i = index_of(address);
    if (i < 0) {
        return false;
    }
    info.address = ports[i].daddr;
    info.interfaceNumber = ports[i].itf;
    info.inCableCount = ports[i].rxCables;
    info.outCableCount = ports[i].txCables;
    return true;
}

bool CH32RVUsbHost::midiSend(const uint8_t *data, size_t length, uint8_t address)
{
    const int i = index_of(address);
    if (i < 0) {
        return fail(_started ? CH32RVUsbHostError::NotConnected : CH32RVUsbHostError::NotStarted);
    }
    if (!data || length % 4) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    const uint32_t t0 = millis();
    size_t done = 0;
    while (done < length) {
        done += tuh_midi_packet_write_n((uint8_t)i, data + done, (uint32_t)(length - done));
        tuh_midi_write_flush((uint8_t)i);
        if (done < length) {
            tuh_task_ext(0, false);
            if (!ports[i].mounted || (uint32_t)(millis() - t0) > 1000u) {
                return fail(ports[i].mounted ? CH32RVUsbHostError::Timeout : CH32RVUsbHostError::NotConnected);
            }
        }
    }
    return ok();
}

static bool channel(CH32RVUsbHost &host, uint8_t status, uint8_t d1, uint8_t d2, uint8_t address)
{
    const uint8_t p[4] = {cin_of(status), status, d1, d2};
    return host.midiSend(p, 4, address);
}

bool CH32RVUsbHost::midiSendNoteOn(uint8_t ch, uint8_t note, uint8_t velocity, uint8_t address)
{
    return channel(*this, (uint8_t)(0x90 | (ch & 15)), note & 0x7F, velocity & 0x7F, address);
}
bool CH32RVUsbHost::midiSendNoteOff(uint8_t ch, uint8_t note, uint8_t velocity, uint8_t address)
{
    return channel(*this, (uint8_t)(0x80 | (ch & 15)), note & 0x7F, velocity & 0x7F, address);
}
bool CH32RVUsbHost::midiSendControlChange(uint8_t ch, uint8_t control, uint8_t value, uint8_t address)
{
    return channel(*this, (uint8_t)(0xB0 | (ch & 15)), control & 0x7F, value & 0x7F, address);
}
bool CH32RVUsbHost::midiSendProgramChange(uint8_t ch, uint8_t program, uint8_t address)
{
    return channel(*this, (uint8_t)(0xC0 | (ch & 15)), program & 0x7F, 0, address);
}
bool CH32RVUsbHost::midiSendPolyPressure(uint8_t ch, uint8_t note, uint8_t pressure, uint8_t address)
{
    return channel(*this, (uint8_t)(0xA0 | (ch & 15)), note & 0x7F, pressure & 0x7F, address);
}
bool CH32RVUsbHost::midiSendChannelPressure(uint8_t ch, uint8_t pressure, uint8_t address)
{
    return channel(*this, (uint8_t)(0xD0 | (ch & 15)), pressure & 0x7F, 0, address);
}
bool CH32RVUsbHost::midiSendPitchBend(uint8_t ch, uint16_t value, uint8_t address)
{
    return channel(*this, (uint8_t)(0xE0 | (ch & 15)), value & 0x7F, (value >> 7) & 0x7F, address);
}
bool CH32RVUsbHost::midiSendPitchBendSigned(uint8_t ch, int16_t value, uint8_t address)
{
    const int32_t v = value < -8192 ? -8192 : value > 8191 ? 8191 : value;
    return midiSendPitchBend(ch, (uint16_t)(v + 8192), address);
}

/* SysEx in USB-MIDI packets: CIN 4 for three bytes that do not end it, 5 / 6 /
 * 7 for the packet holding F7 with one / two / three bytes. */
bool CH32RVUsbHost::midiSendSysEx(const uint8_t *data, size_t length, uint8_t address)
{
    if (!data || length < 2 || data[0] != 0xF0 || data[length - 1] != 0xF7) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    size_t at = 0;
    while (at < length) {
        const size_t left = length - at;
        uint8_t p[4] = {0, 0, 0, 0};
        if (left > 3) {
            p[0] = 0x4;
            memcpy(p + 1, data + at, 3);
            at += 3;
        } else {
            p[0] = (uint8_t)(0x4 + left);
            memcpy(p + 1, data + at, left);
            at += left;
        }
        if (!midiSend(p, 4, address)) {
            return false;
        }
    }
    return ok();
}

#else /* no host */

void CH32RVUsbHost::onMidiMessage(MidiMessageCallback) {}
bool CH32RVUsbHost::midiReady(uint8_t) const { return false; }
bool CH32RVUsbHost::getMidiPortInfo(CH32RVUsbHostMidiPortInfo &, uint8_t) const { return false; }
bool CH32RVUsbHost::midiSend(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendNoteOn(uint8_t, uint8_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendNoteOff(uint8_t, uint8_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendControlChange(uint8_t, uint8_t, uint8_t, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::midiSendProgramChange(uint8_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendPolyPressure(uint8_t, uint8_t, uint8_t, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::midiSendChannelPressure(uint8_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendPitchBend(uint8_t, uint16_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendPitchBendSigned(uint8_t, int16_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::midiSendSysEx(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }

#endif
