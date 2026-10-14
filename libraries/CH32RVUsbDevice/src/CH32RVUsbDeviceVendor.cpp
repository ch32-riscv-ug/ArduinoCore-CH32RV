/* Vendor (bulk): TinyUSB's vendor_device, buffered. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

const char *CH32RVUsbDeviceVendor::lastDirectErrorName() const
{
    switch (_directError) {
    case CH32RVUsbDeviceVendorDirectError::None: return "None";
    case CH32RVUsbDeviceVendorDirectError::NotSupported: return "NotSupported";
    case CH32RVUsbDeviceVendorDirectError::NotMounted: return "NotMounted";
    case CH32RVUsbDeviceVendorDirectError::BadArgument: return "BadArgument";
    case CH32RVUsbDeviceVendorDirectError::NotAligned: return "NotAligned";
    case CH32RVUsbDeviceVendorDirectError::NotDmaCapable: return "NotDmaCapable";
    case CH32RVUsbDeviceVendorDirectError::Busy: return "Busy";
    case CH32RVUsbDeviceVendorDirectError::TransferFailed: return "TransferFailed";
    }
    return "Unknown";
}

bool CH32RVUsbDeviceVendor::writeDirect(const void *, size_t)
{
    _directError = CH32RVUsbDeviceVendorDirectError::NotSupported;
    return false;
}

static uint16_t clamp_ep(uint16_t n)
{
    return n >= 64 ? 64 : n >= 32 ? 32 : n >= 16 ? 16 : 8;
}

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t vendor_driver = {
    NULL, vendord_init, vendord_deinit, vendord_reset, vendord_open, vendord_control_xfer_cb, vendord_xfer_cb, NULL,
    NULL,
};

static CH32RVUsbDeviceVendor *vendors[CFG_TUD_VENDOR];

static uint16_t vendor_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                                  uint8_t *ep, uint8_t str)
{
    const CH32RVUsbDeviceVendor &v = static_cast<const CH32RVUsbDeviceVendor &>(self);
    const uint8_t n = (*ep)++;
    const uint8_t d[] = {TUD_VENDOR_DESCRIPTOR(itf, str, n, (uint8_t)(0x80 | n), v.endpointSize())};
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

/* winusb 2: the compatible ID and DeviceInterfaceGUIDs. */
static const CH32RVUsbFunctionType vendor_type = {&vendor_driver, vendor_descriptor, 1, CFG_TUD_VENDOR, nullptr, 2};

static bool vendor_request(uint8_t rhport, uint8_t stage, const void *request)
{
    for (uint8_t i = 0; i < CFG_TUD_VENDOR; i++) {
        if (vendors[i] && vendors[i]->handleControlRequest(rhport, stage, request)) {
            return true;
        }
    }
    return false;
}

CH32RVUsbDeviceVendor::CH32RVUsbDeviceVendor(CH32RVUsbDevice &device, uint16_t endpointSize)
    : CH32RVUsbDeviceFunction(device, vendor_type, CH32RV_USB_VENDOR_NAME), _endpointSize(clamp_ep(endpointSize))
{
    if (attached()) {
        vendors[instance()] = this;
        ch32rv_usb_platform_use();
        ch32rv_usb_platform.vendorRequest = vendor_request;
    }
}

bool CH32RVUsbDeviceVendor::mounted() const
{
    return attached() && device().started() && tud_vendor_n_mounted(instance());
}

int CH32RVUsbDeviceVendor::available()
{
    ch32rv_usb_task();
    return attached() && device().started() ? (int)tud_vendor_n_available(instance()) : 0;
}

int CH32RVUsbDeviceVendor::read()
{
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

size_t CH32RVUsbDeviceVendor::read(uint8_t *buffer, size_t size)
{
    ch32rv_usb_task();
    if (!attached() || !device().started() || !buffer) {
        return 0;
    }
    return tud_vendor_n_read(instance(), buffer, (uint32_t)size);
}

size_t CH32RVUsbDeviceVendor::write(uint8_t data)
{
    return write(&data, 1);
}

/* Takes what fits now, as EspUsbDevice does: the return value says how much. */
size_t CH32RVUsbDeviceVendor::write(const uint8_t *buffer, size_t size)
{
    if (!mounted() || !buffer) {
        return 0;
    }
    const uint32_t n = tud_vendor_n_write(instance(), buffer, (uint32_t)size);
    ch32rv_usb_task();
    return n;
}

size_t CH32RVUsbDeviceVendor::writeAvailable() const
{
    return mounted() ? tud_vendor_n_write_available(instance()) : 0;
}

size_t CH32RVUsbDeviceVendor::writeCapacity()
{
    return CFG_TUD_VENDOR_TX_BUFSIZE;
}

bool CH32RVUsbDeviceVendor::waitWritable(size_t bytes, uint32_t timeoutMs)
{
    if (bytes > writeCapacity()) {
        bytes = writeCapacity();
    }
    flush();
    const uint32_t t0 = millis();
    for (;;) {
        if (!mounted()) {
            return false;
        }
        if (writeAvailable() >= bytes) {
            return true;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
        ch32rv_usb_task();
    }
}

void CH32RVUsbDeviceVendor::flush()
{
    if (mounted()) {
        tud_vendor_n_write_flush(instance());
        ch32rv_usb_task();
    }
}

bool CH32RVUsbDeviceVendor::sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request,
                                                const void *data, size_t length)
{
    const tusb_control_request_t *raw = static_cast<const tusb_control_request_t *>(request.rawRequest);
    if (!raw) {
        return false;
    }
    if (!data || !length) {
        return tud_control_status(request.rhport, raw);
    }
    return tud_control_xfer(request.rhport, raw, const_cast<void *>(data), (uint16_t)length);
}

void CH32RVUsbDeviceVendor::handleRx()
{
    if (_onRx) {
        _onRx(tud_vendor_n_available(instance()));
    }
}

void CH32RVUsbDeviceVendor::handleTxComplete(size_t sentBytes)
{
    if (_onTxComplete) {
        _onTxComplete(sentBytes);
    }
}

bool CH32RVUsbDeviceVendor::handleControlRequest(uint8_t rhport, uint8_t stage, const void *request)
{
    const tusb_control_request_t *raw = static_cast<const tusb_control_request_t *>(request);
    if (!_onControl) {
        return false;
    }
    CH32RVUsbDeviceVendorControlRequest r;
    r.rhport = rhport;
    r.stage = stage;
    r.bmRequestType = raw->bmRequestType;
    r.bRequest = raw->bRequest;
    r.wValue = raw->wValue;
    r.wIndex = raw->wIndex;
    r.wLength = raw->wLength;
    r.rawRequest = raw;
    return _onControl(r);
}

extern "C" void tud_vendor_rx_cb(uint8_t idx, const uint8_t *buffer, uint32_t bufsize)
{
    (void)buffer;
    (void)bufsize;
    if (idx < CFG_TUD_VENDOR && vendors[idx]) {
        vendors[idx]->handleRx();
    }
}

extern "C" void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes)
{
    if (idx < CFG_TUD_VENDOR && vendors[idx]) {
        vendors[idx]->handleTxComplete(sent_bytes);
    }
}

#else

static uint16_t vendor_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType vendor_type = {nullptr, vendor_descriptor, 1, 1, nullptr, 2};

CH32RVUsbDeviceVendor::CH32RVUsbDeviceVendor(CH32RVUsbDevice &device, uint16_t endpointSize)
    : CH32RVUsbDeviceFunction(device, vendor_type, CH32RV_USB_VENDOR_NAME), _endpointSize(clamp_ep(endpointSize))
{
}

bool CH32RVUsbDeviceVendor::mounted() const { return false; }
int CH32RVUsbDeviceVendor::available() { return 0; }
int CH32RVUsbDeviceVendor::read() { return -1; }
size_t CH32RVUsbDeviceVendor::read(uint8_t *, size_t) { return 0; }
size_t CH32RVUsbDeviceVendor::write(uint8_t) { return 0; }
size_t CH32RVUsbDeviceVendor::write(const uint8_t *, size_t) { return 0; }
size_t CH32RVUsbDeviceVendor::writeAvailable() const { return 0; }
size_t CH32RVUsbDeviceVendor::writeCapacity() { return 0; }
bool CH32RVUsbDeviceVendor::waitWritable(size_t, uint32_t) { return false; }
void CH32RVUsbDeviceVendor::flush() {}
bool CH32RVUsbDeviceVendor::sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &, const void *, size_t)
{
    return false;
}
void CH32RVUsbDeviceVendor::handleRx() {}
void CH32RVUsbDeviceVendor::handleTxComplete(size_t) {}
bool CH32RVUsbDeviceVendor::handleControlRequest(uint8_t, uint8_t, const void *) { return false; }

#endif
