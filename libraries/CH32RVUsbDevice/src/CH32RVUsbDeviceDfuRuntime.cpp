/* DFU runtime (DFU 1.1): DFU_DETACH resets into the factory USB bootloader.
 *
 * Windows has no in-box driver for a DFU interface; dfu-util (libusb) needs
 * WinUSB on it, which the Microsoft OS 2.0 set names
 * (CH32RVUsbDevicePlatform.cpp: compatible ID only, no DeviceInterfaceGUIDs -
 * libusb opens a WinUSB device without one). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define DFU_DETACH    0
#define DFU_GETSTATUS 3
#define DFU_GETSTATE  5

static uint16_t dfu_rt_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                                  uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    (void)self;
    (void)ep;
    (void)speed;
    /* WILL_DETACH only: the device leaves the bus by itself after
     * DFU_DETACH. No CAN_DOWNLOAD - what appears next is the factory ISP
     * bootloader, not a DFU-mode device, so a DFU download (dfu-util -D)
     * cannot work and the descriptor must not claim it. Detach timeout
     * 1000 ms, transfer size 64, DFU 1.1. */
    const uint8_t d[] = {
        9, 4, itf, 0, 0, 0xFE, 0x01, 0x01, str,
        9, 0x21, 0x08, 0xE8, 0x03, 64, 0, 0x10, 0x01,
    };
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType dfu_rt_type = {dfu_rt_descriptor, 1, 1,
                                                  &CH32RVUsbDeviceConfig::dfuRuntimeName, 1};

static CH32RVUsbDeviceDfuRuntime *dfu;

CH32RVUsbDeviceDfuRuntime::CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, dfu_rt_type, CH32RV_USB_DFU_RUNTIME_NAME)
{
    if (attached()) {
        dfu = this;
        ch32rv_usb_platform_use();
    }
}

void CH32RVUsbDeviceDfuRuntime::attachDefault(CH32RVUsbDevice &device)
{
    if (!dfu) {
        static CH32RVUsbDeviceDfuRuntime builtin(device);
        (void)builtin;
    }
}

void CH32RVUsbDeviceDfuRuntime::detach()
{
    if (_onDetach && !_onDetach()) {
        return;
    }
    CH32RVUsbDevice::requestBootloader();
}

bool CH32RVUsbDeviceDfuRuntime::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    switch (s.bRequest) {
    case DFU_DETACH:                             /* leave once the status stage is out */
        if (stage == CH32RVUsbControlStage::Setup) {
            return device().controlStatus();
        }
        if (stage == CH32RVUsbControlStage::Ack) {
            detach();
        }
        return true;
    case DFU_GETSTATUS:
        return stage != CH32RVUsbControlStage::Setup || device().controlIn(_status, sizeof _status);
    case DFU_GETSTATE:
        return stage != CH32RVUsbControlStage::Setup || device().controlIn(_status + 4, 1);
    }
    return false;
}
