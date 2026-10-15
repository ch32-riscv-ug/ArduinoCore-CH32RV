/* DFU runtime: TinyUSB's dfu_rt_device; DFU_DETACH resets into the factory
 * USB bootloader. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t dfu_rt_driver = {
    NULL, dfu_rtd_init, dfu_rtd_deinit, dfu_rtd_reset, dfu_rtd_open, dfu_rtd_control_xfer_cb, NULL, NULL, NULL,
};

/* Windows has no in-box driver for a DFU interface; dfu-util (libusb) needs
 * WinUSB on it, which the Microsoft OS 2.0 set names
 * (CH32RVUsbDevicePlatform.cpp: compatible ID only, no DeviceInterfaceGUIDs -
 * libusb opens a WinUSB device without one). */

static uint16_t dfu_rt_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                                  uint8_t *ep, uint8_t str, bool hs)
{
    (void)self;
    (void)ep;
    (void)hs;
    /* WILL_DETACH only: the device leaves the bus by itself after
     * DFU_DETACH. No CAN_DOWNLOAD - what appears next is the factory ISP
     * bootloader, not a DFU-mode device, so a DFU download (dfu-util -D)
     * cannot work and the descriptor must not claim it. */
    const uint8_t d[] = {TUD_DFU_RT_DESCRIPTOR(itf, str, DFU_ATTR_WILL_DETACH, 1000, 64)};
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType dfu_rt_type = {&dfu_rt_driver, dfu_rt_descriptor, 1, CFG_TUD_DFU_RUNTIME, &CH32RVUsbDeviceConfig::dfuRuntimeName, 1};

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

extern "C" void tud_dfu_runtime_reboot_to_dfu_cb(void)
{
    if (dfu) {
        dfu->detach();
    }
}

#else

static uint16_t dfu_rt_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t, bool)
{
    return 0;
}

static const CH32RVUsbFunctionType dfu_rt_type = {nullptr, dfu_rt_descriptor, 1, 1, nullptr, 0};

CH32RVUsbDeviceDfuRuntime::CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, dfu_rt_type, CH32RV_USB_DFU_RUNTIME_NAME)
{
}

void CH32RVUsbDeviceDfuRuntime::detach() {}
void CH32RVUsbDeviceDfuRuntime::attachDefault(CH32RVUsbDevice &) {}

#endif
