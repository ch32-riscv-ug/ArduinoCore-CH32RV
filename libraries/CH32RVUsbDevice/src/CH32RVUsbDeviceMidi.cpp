/* USB MIDI: TinyUSB's midi_device. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t midi_driver = {
    NULL, midid_init, midid_deinit, midid_reset, midid_open, midid_control_xfer_cb, midid_xfer_cb, NULL, NULL,
};

/* Audio control + MIDI streaming (two interfaces), OUT and IN on endpoint n. */
static uint16_t midi_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint8_t itf,
                                uint8_t *ep, uint8_t str)
{
    (void)self;
    const uint8_t n = (*ep)++;
    const uint8_t d[] = {TUD_MIDI_DESCRIPTOR(itf, str, n, (uint8_t)(0x80 | n), 64)};
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType midi_type = {&midi_driver, midi_descriptor, 2, CFG_TUD_MIDI};

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI")
{
}

/* A channel message on cable 0: code index = the status's high nibble. */
static bool channel_message(uint8_t itf, uint8_t status, uint8_t channel, uint8_t d1, uint8_t d2)
{
    if (channel < 1 || channel > 16) {
        return false;
    }
    const uint8_t s = (uint8_t)(status | (channel - 1));
    const uint8_t packet[4] = {(uint8_t)(status >> 4), s, (uint8_t)(d1 & 0x7F), (uint8_t)(d2 & 0x7F)};
    ch32rv_usb_task();
    return tud_midi_n_mounted(itf) && tud_midi_n_packet_write(itf, packet);
}

bool CH32RVUsbDeviceMidi::noteOn(uint8_t note, uint8_t velocity, uint8_t channel)
{
    return channel_message(instance(), 0x90, channel, note, velocity);
}

bool CH32RVUsbDeviceMidi::noteOff(uint8_t note, uint8_t velocity, uint8_t channel)
{
    return channel_message(instance(), 0x80, channel, note, velocity);
}

bool CH32RVUsbDeviceMidi::controlChange(uint8_t controller, uint8_t value, uint8_t channel)
{
    return channel_message(instance(), 0xB0, channel, controller, value);
}

bool CH32RVUsbDeviceMidi::programChange(uint8_t program, uint8_t channel)
{
    return channel_message(instance(), 0xC0, channel, program, 0);
}

bool CH32RVUsbDeviceMidi::pitchBend(int16_t value, uint8_t channel)
{
    const uint16_t v = (uint16_t)(value + 8192);
    return channel_message(instance(), 0xE0, channel, (uint8_t)(v & 0x7F), (uint8_t)((v >> 7) & 0x7F));
}

bool CH32RVUsbDeviceMidi::writePacket(const uint8_t packet[4])
{
    ch32rv_usb_task();
    return tud_midi_n_mounted(instance()) && tud_midi_n_packet_write(instance(), packet);
}

uint32_t CH32RVUsbDeviceMidi::available()
{
    ch32rv_usb_task();
    return tud_midi_n_available(instance(), 0) / 4u;
}

bool CH32RVUsbDeviceMidi::readPacket(uint8_t packet[4])
{
    ch32rv_usb_task();
    return tud_midi_n_packet_read(instance(), packet);
}

#else

static uint16_t midi_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType midi_type = {nullptr, midi_descriptor, 2, 1};

CH32RVUsbDeviceMidi::CH32RVUsbDeviceMidi(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, midi_type, "MIDI")
{
}

bool CH32RVUsbDeviceMidi::noteOn(uint8_t, uint8_t, uint8_t) { return false; }
bool CH32RVUsbDeviceMidi::noteOff(uint8_t, uint8_t, uint8_t) { return false; }
bool CH32RVUsbDeviceMidi::controlChange(uint8_t, uint8_t, uint8_t) { return false; }
bool CH32RVUsbDeviceMidi::programChange(uint8_t, uint8_t) { return false; }
bool CH32RVUsbDeviceMidi::pitchBend(int16_t, uint8_t) { return false; }
bool CH32RVUsbDeviceMidi::writePacket(const uint8_t *) { return false; }
uint32_t CH32RVUsbDeviceMidi::available() { return 0; }
bool CH32RVUsbDeviceMidi::readPacket(uint8_t *) { return false; }

#endif
