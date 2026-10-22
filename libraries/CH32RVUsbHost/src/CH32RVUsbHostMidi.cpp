/* CH32RVUsbHost: USB MIDI 1.0. Messages are the 4-byte USB-MIDI event
 * packets; onMidiMessage() gets them decoded. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

#define RX_PACKETS 32

struct Port {
    bool mounted;
    uint8_t daddr, itf, rxCables, txCables;
    uint8_t epIn, epOut;
    uint16_t mpsIn, mpsOut;
    volatile bool inBusy, outDone;
    volatile uint8_t outResult;
    volatile uint8_t head, tail;               /* rx ring of packets */
    uint8_t rx[RX_PACKETS][4];
    uint8_t in[64] __attribute__((aligned(4)));
};
Port ports[1];
CH32RVUsbHost::MidiMessageCallback message_cb;

uint16_t midi_open(uint8_t daddr, const uint8_t *itf, uint16_t length);
void midi_configured(uint8_t daddr, uint8_t itf);
size_t deliver(CH32RVUsbHost &host);
void midi_detach(uint8_t daddr);
CH32RVUsbHostGroup midi_group = {midi_open, midi_configured, nullptr, midi_detach, deliver, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(midi_group); }
} join;

uint8_t rx_free(const Port &p)
{
    return (uint8_t)(RX_PACKETS - 1 - (uint8_t)((p.head + RX_PACKETS - p.tail) % RX_PACKETS));
}

void in_done(void *user, uint8_t result, uint16_t length);

/* Armed only with room for a whole packet's worth: otherwise the device is
 * NAKed until the sketch has read. */
void arm_in(Port &p)
{
    if (!p.mounted || p.inBusy || rx_free(p) < p.mpsIn / 4) {
        return;
    }
    p.inBusy = ch32rv_usb_host_xfer(p.daddr, p.epIn, p.in, p.mpsIn, in_done, &p);
}

void in_done(void *user, uint8_t result, uint16_t length)
{
    Port &p = *(Port *)user;
    p.inBusy = false;
    if (!p.mounted) {
        return;
    }
    if (result == CH32RV_USBH_OK) {
        for (uint16_t at = 0; at + 4 <= length; at += 4) {
            if (p.in[at] == 0 && p.in[at + 1] == 0) {
                continue;              /* padding */
            }
            const uint8_t next = (uint8_t)((p.head + 1) % RX_PACKETS);
            if (next == p.tail) {
                break;
            }
            memcpy(p.rx[p.head], p.in + at, 4);
            p.head = next;
        }
    }
    arm_in(p);
}

/* A MIDI streaming interface (audio class, subclass 3), with the audio
 * control interface in front of it when there is one: both are taken. The
 * cable counts are the embedded jacks the class-specific endpoint
 * descriptors name. */
uint16_t midi_open(uint8_t daddr, const uint8_t *itf, uint16_t length)
{
    if (itf[5] != 1 || ports[0].daddr) {
        return 0;
    }
    uint16_t at = 0;
    if (itf[6] == 1) {                 /* audio control: the streaming interface follows */
        at = itf[0];
        while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4) {
            at = (uint16_t)(at + itf[at]);
        }
        if (at + 9 > length || itf[at + 5] != 1 || itf[at + 6] != 3) {
            return 0;
        }
    } else if (itf[6] != 3) {
        return 0;
    }
    Port p = {};
    const uint8_t *ms = itf + at;
    p.daddr = daddr;
    p.itf = ms[2];
    uint16_t k = (uint16_t)(at + ms[0]);
    uint8_t last = 0;                  /* the endpoint the next class-specific descriptor is about */
    while (k + 2 <= length && itf[k] >= 2 && itf[k + 1] != 4 && itf[k + 1] != 11) {
        const uint8_t *d = itf + k;
        if (d[1] == 5 && d[0] >= 7 && (d[3] & 3) == 2) {
            last = d[2];
            const uint16_t mps = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
            if ((d[2] & 0x80) && !p.epIn && ch32rv_usb_host_ep_open(daddr, d)) {
                p.epIn = d[2];
                p.mpsIn = mps < sizeof p.in ? mps : sizeof p.in;
            } else if (!(d[2] & 0x80) && !p.epOut && ch32rv_usb_host_ep_open(daddr, d)) {
                p.epOut = d[2];
                p.mpsOut = mps;
            }
        } else if (d[1] == 0x25 && d[0] >= 4 && d[2] == 1) {   /* CS_ENDPOINT, MS_GENERAL */
            if (last & 0x80) {
                p.rxCables = d[3];
            } else if (last) {
                p.txCables = d[3];
            }
        }
        k = (uint16_t)(k + d[0]);
    }
    if (!p.epIn && !p.epOut) {
        return 0;
    }
    ports[0] = p;
    return k;
}

void midi_configured(uint8_t daddr, uint8_t itf)
{
    Port &p = ports[0];
    if (p.daddr != daddr || p.mounted || (itf != p.itf && itf + 1 != p.itf)) {
        return;
    }
    p.mounted = true;
    ch32rv_usb_host_mark_claimed(daddr, p.itf);
    if (p.itf) {
        ch32rv_usb_host_mark_claimed(daddr, (uint8_t)(p.itf - 1u));   /* audio control */
    }
    if (p.epIn) {
        arm_in(p);
    }
}

void midi_detach(uint8_t daddr)
{
    if (ports[0].daddr == daddr) {
        memset(&ports[0], 0, sizeof ports[0]);
    }
}

/* The packets wait in the ring until here; a full one holds the device off. */
size_t deliver(CH32RVUsbHost &host)
{
    (void)host;
    size_t ran = 0;
    Port &p = ports[0];
    if (!p.mounted || !message_cb) {
        return ran;
    }
    while (p.mounted && p.tail != p.head) {
        uint8_t pk[4];
        memcpy(pk, p.rx[p.tail], 4);
        p.tail = (uint8_t)((p.tail + 1) % RX_PACKETS);
        CH32RVUsbHostMidiMessage m;
        m.address = p.daddr;
        m.interfaceNumber = p.itf;
        m.cable = (uint8_t)(pk[0] >> 4);
        m.codeIndex = (uint8_t)(pk[0] & 0x0F);
        m.status = pk[1];
        m.data1 = pk[2];
        m.data2 = pk[3];
        m.raw = pk;
        m.length = 4;
        message_cb(m);
        ran++;
    }
    if (p.mounted && p.epIn) {
        arm_in(p);
    }
    return ran;
}

void out_done(void *user, uint8_t result, uint16_t length)
{
    (void)length;
    Port &p = *(Port *)user;
    p.outResult = result;
    p.outDone = true;
}

int index_of(uint8_t address)
{
    for (int i = 0; i < (int)(sizeof ports / sizeof ports[0]); i++) {
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
    Port &p = ports[i];
    if (!p.epOut) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    size_t done = 0;
    while (done < length) {
        const size_t room = p.mpsOut - p.mpsOut % 4;
        const size_t n = length - done < room ? length - done : room;
        p.outDone = false;
        if (!ch32rv_usb_host_xfer(p.daddr, p.epOut, const_cast<uint8_t *>(data + done), (uint16_t)n, out_done, &p)) {
            return fail(CH32RVUsbHostError::TransferFailed);
        }
        if (!ch32rv_usb_host_wait(p.outDone, 1000)) {
            ch32rv_usb_host_abort(p.daddr, p.epOut);
            return fail(p.mounted ? CH32RVUsbHostError::Timeout : CH32RVUsbHostError::NotConnected);
        }
        if (p.outResult != CH32RV_USBH_OK) {
            return fail(ch32rv_usb_host_result_error(p.outResult));
        }
        done += n;
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
