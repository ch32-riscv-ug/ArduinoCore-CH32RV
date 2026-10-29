/* Vendor (bulk) on the device stack: the bulk pair is CH32RVUsbDeviceBulkStream's. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

static uint16_t clamp_ep(uint16_t n)
{
    return n >= 64 ? 64 : n >= 32 ? 32 : n >= 16 ? 16 : 8;
}

#define VENDORS_MAX 2

static CH32RVUsbDeviceVendor *vendors[VENDORS_MAX];

static bool vendor_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                                  uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceVendor &v = const_cast<CH32RVUsbDeviceVendor &>(static_cast<const CH32RVUsbDeviceVendor &>(self));
    if (!v.receiveReady()) {
        return v.refuseSettings();
    }
    const uint8_t n = (*ep)++;
    v._ep = n;
    const uint16_t mps = speed == CH32RVUsbSpeed::Full ? v.endpointSize() : ch32rv_usb_bulk_size(speed);
    const uint8_t d[] = {
        9, 4, itf, 0, 2, 0xFF, v.interfaceSubclass(), v.interfaceProtocol(), str,
        7, 5, n, 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
        7, 5, (uint8_t)(0x80 | n), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
    };
    w.put(d, sizeof d);
    return true;
}

/* winusb 2: the compatible ID and DeviceInterfaceGUIDs. */
static const CH32RVUsbFunctionType vendor_type = {vendor_descriptor, 1, VENDORS_MAX, nullptr, 2};

/* Vendor requests to the device that the platform descriptors do not answer. */
static bool vendor_request(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    for (uint8_t i = 0; i < VENDORS_MAX; i++) {
        if (vendors[i] && vendors[i]->handleControlRequest(stage, setup)) {
            return true;
        }
    }
    return false;
}

CH32RVUsbDeviceVendor::CH32RVUsbDeviceVendor(CH32RVUsbDevice &device, uint16_t endpointSize)
    : CH32RVUsbDeviceBulkStream(device, vendor_type, CH32RV_USB_VENDOR_NAME, false),
      _endpointSize(clamp_ep(endpointSize))
{
    if (attached()) {
        vendors[instance()] = this;
        ch32rv_usb_platform_use();
        ch32rv_usb_platform.vendorControl = vendor_request;
    }
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceVendor::usbConfigured(CH32RVUsbSpeed speed)
{
    bulkConfigured(speed == CH32RVUsbSpeed::Full ? _endpointSize : ch32rv_usb_bulk_size(speed));
}

bool CH32RVUsbDeviceVendor::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    return (setup.bmRequestType & 0x60) == 0x40 && handleControlRequest(stage, setup);
}

/* ---------------------------------------------------------- the API */
int CH32RVUsbDeviceVendor::read()
{
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

size_t CH32RVUsbDeviceVendor::write(uint8_t data)
{
    return write(&data, 1);
}

size_t CH32RVUsbDeviceVendor::write(const uint8_t *buffer, size_t size)
{
    const size_t n = bulkQueue(buffer, size);
    ch32rv_usb_task();
    return n;
}

bool CH32RVUsbDeviceVendor::waitWritable(size_t bytes, uint32_t timeoutMs)
{
    if (bytes > writeCapacity()) {
        return false;
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
    push();
    ch32rv_usb_task();
}

/* Answer at stage SETUP: data, or a status stage when none. */
bool CH32RVUsbDeviceVendor::sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request,
                                                const void *data, size_t length)
{
    if (request.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
        return false;
    }
    if (!data || !length) {
        return device().controlStatus();
    }
    return device().controlIn(data, (uint16_t)length);
}

bool CH32RVUsbDeviceVendor::handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    if (!_onControl) {
        return false;
    }
    CH32RVUsbDeviceVendorControlRequest r;
    r.stage = stage == CH32RVUsbControlStage::Setup ? CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP
              : stage == CH32RVUsbControlStage::Data ? CH32RV_USB_DEVICE_CONTROL_STAGE_DATA
                                                     : CH32RV_USB_DEVICE_CONTROL_STAGE_ACK;
    r.bmRequestType = setup.bmRequestType;
    r.bRequest = setup.bRequest;
    r.wValue = setup.wValue;
    r.wIndex = setup.wIndex;
    r.wLength = setup.wLength;
    r.actualBytes = device().controlActualBytes();
    r.rawRequest = &setup;
    return _onControl(r);
}
