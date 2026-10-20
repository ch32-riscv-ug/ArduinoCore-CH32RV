/* CH32RVUsbHost: a vendor-specific bulk interface (WebUSB devices, the
 * CH32RVUsbDevice / EspUsbDevice vendor function, instruments). One open at a
 * time. Vendor control requests are in CH32RVUsbHost.cpp. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

CH32RVUsbHostPipe pipe;
uint8_t own_ring[CH32RV_USB_HOST_VENDOR_RX_BUFFER_SIZE];
uint8_t *ring = own_ring;
uint16_t ring_size = sizeof own_ring;

void poll(CH32RVUsbHost &host)
{
    ch32rv_usb_host_pipe_poll(pipe);
    if (!host._vendorData || !pipe.open) {
        return;
    }
    uint8_t chunk[64];
    uint16_t n;
    while ((n = ch32rv_usb_host_pipe_read(pipe, chunk, sizeof chunk)) != 0) {
        CH32RVUsbHostVendorData d;
        d.address = pipe.daddr;
        d.interfaceNumber = pipe.itf;
        d.endpoint = pipe.epIn;
        d.data = chunk;
        d.length = n;
        host._vendorData(d);
    }
}

void detach(uint8_t daddr)
{
    if (pipe.open && pipe.daddr == daddr) {
        pipe.open = false;
        pipe.inBusy = false;
    }
}

CH32RVUsbHostGroup vendor_group = {nullptr, nullptr, poll, detach, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(vendor_group); }
} join;

bool matches(uint8_t address)
{
    return pipe.open && (address == CH32RV_USB_HOST_ANY_ADDRESS || pipe.daddr == address);
}

}  // namespace

bool CH32RVUsbHost::setVendorBuffer(uint8_t *buffer, size_t size)
{
    if (!buffer || size < 65 || size > 0xFFFF || pipe.open) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    ring = buffer;
    ring_size = (uint16_t)size;
    return ok();
}

bool CH32RVUsbHost::vendorOpen(uint8_t address, uint8_t interfaceNumber)
{
    if (!_started) {
        return fail(CH32RVUsbHostError::NotStarted);
    }
    /* ANY: the first device that has a vendor interface. */
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if (address != CH32RV_USB_HOST_ANY_ADDRESS && a != address) {
            continue;
        }
        if (!ch32rv_usb_host_device(a)) {
            continue;
        }
        if (ch32rv_usb_host_pipe_open(pipe, a, interfaceNumber, 0xFF, ring, ring_size)) {
            return ok();
        }
    }
    return fail(CH32RVUsbHostError::NotSupported);
}

void CH32RVUsbHost::vendorClose()
{
    ch32rv_usb_host_pipe_close(pipe);
}

bool CH32RVUsbHost::vendorReady(uint8_t address) const
{
    return matches(address);
}

bool CH32RVUsbHost::getVendorInterface(CH32RVUsbHostVendorInterface &info) const
{
    if (!pipe.open) {
        return false;
    }
    info.address = pipe.daddr;
    info.interfaceNumber = pipe.itf;
    info.inEndpoint = pipe.epIn;
    info.outEndpoint = pipe.epOut;
    info.inMaxPacketSize = pipe.mpsIn;
    info.outMaxPacketSize = pipe.mpsOut;
    return true;
}

bool CH32RVUsbHost::vendorWrite(const uint8_t *data, size_t length, uint8_t address, uint32_t timeoutMs)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return ch32rv_usb_host_pipe_write(*this, pipe, data, length, timeoutMs);
}

size_t CH32RVUsbHost::vendorAvailable() const
{
    return pipe.open ? ch32rv_usb_host_pipe_available(pipe) : 0;
}

size_t CH32RVUsbHost::vendorRead(uint8_t *buffer, size_t length, uint8_t address)
{
    if (!matches(address) || !buffer) {
        return 0;
    }
    ch32rv_usb_host_task();
    return ch32rv_usb_host_pipe_read(pipe, buffer, (uint16_t)ch32rv_min(length, (size_t)0xFFFF));
}

uint16_t CH32RVUsbHost::vendorOutPacketSize(uint8_t address) const
{
    return matches(address) ? pipe.mpsOut : 0;
}

uint16_t CH32RVUsbHost::vendorInPacketSize(uint8_t address) const
{
    return matches(address) ? pipe.mpsIn : 0;
}

#else /* no host */

bool CH32RVUsbHost::setVendorBuffer(uint8_t *, size_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::vendorOpen(uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::vendorClose() {}
bool CH32RVUsbHost::vendorReady(uint8_t) const { return false; }
bool CH32RVUsbHost::getVendorInterface(CH32RVUsbHostVendorInterface &) const { return false; }
bool CH32RVUsbHost::vendorWrite(const uint8_t *, size_t, uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
size_t CH32RVUsbHost::vendorAvailable() const { return 0; }
size_t CH32RVUsbHost::vendorRead(uint8_t *, size_t, uint8_t) { return 0; }
uint16_t CH32RVUsbHost::vendorOutPacketSize(uint8_t) const { return 0; }
uint16_t CH32RVUsbHost::vendorInPacketSize(uint8_t) const { return 0; }

#endif
