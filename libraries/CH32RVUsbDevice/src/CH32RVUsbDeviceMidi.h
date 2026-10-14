/* USB MIDI: one cable in and out, as 4-byte USB-MIDI event packets, with
 * helpers for the common channel messages. Included by CH32RVUsbDevice.h.
 * Channels are 1..16, as musicians count them. */
#pragma once

class CH32RVUsbDeviceMidi : public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceMidi(CH32RVUsbDevice &device = USBDevice);

    bool noteOn(uint8_t note, uint8_t velocity = 100, uint8_t channel = 1);
    bool noteOff(uint8_t note, uint8_t velocity = 0, uint8_t channel = 1);
    bool controlChange(uint8_t controller, uint8_t value, uint8_t channel = 1);
    bool programChange(uint8_t program, uint8_t channel = 1);
    bool pitchBend(int16_t value, uint8_t channel = 1);   /* -8192 .. 8191 */

    /* Raw USB-MIDI event packets (cable number and code index in byte 0). */
    bool writePacket(const uint8_t packet[4]);
    /* Packets waiting from the host. */
    uint32_t available();
    bool readPacket(uint8_t packet[4]);
};
