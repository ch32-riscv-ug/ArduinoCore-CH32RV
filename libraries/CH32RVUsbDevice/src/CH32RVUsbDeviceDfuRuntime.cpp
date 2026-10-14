/* DFU runtime: TinyUSB's dfu_rt_device; DFU_DETACH resets into the factory
 * USB bootloader. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t dfu_rt_driver = {
    NULL, dfu_rtd_init, dfu_rtd_deinit, dfu_rtd_reset, dfu_rtd_open, dfu_rtd_control_xfer_cb, NULL, NULL, NULL,
};

/* ---------------------------------------------------------- Windows
 * Windows has no in-box driver for a DFU interface; dfu-util (libusb) needs
 * WinUSB on it. A Microsoft OS 2.0 descriptor set naming WINUSB as the
 * interface's compatible ID makes Windows bind it with no driver install. It
 * is announced in a BOS descriptor, which a host only reads from a USB 2.1
 * device - CH32RVUsbDevice reports bcdUSB 2.10 whenever a BOS exists, i.e.
 * only when this file is linked. No DeviceInterfaceGUIDs property: libusb
 * opens a WinUSB device without one. Checked on Linux (descriptors read back);
 * not yet on Windows. */
#define MS_OS_20_VENDOR_CODE 0x01
#define MS_OS_20_SET_LEN     46

static uint8_t ms_os_20_set[MS_OS_20_SET_LEN];

static const uint8_t bos[] = {
    TUD_BOS_DESCRIPTOR(TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_SET_LEN, MS_OS_20_VENDOR_CODE),
};

/* Header, configuration subset, function subset for the DFU interface, and
 * its WINUSB compatible ID (MS OS 2.0 spec, tables 10-13). */
static void build_ms_os_20(uint8_t itf)
{
    static const uint8_t set[MS_OS_20_SET_LEN] = {
        /* set header: Windows 8.1 (0x06030000) and later */
        0x0A, 0x00, MS_OS_20_SET_HEADER_DESCRIPTOR, 0x00, 0x00, 0x00, 0x03, 0x06,
        MS_OS_20_SET_LEN, 0x00,
        /* configuration subset */
        0x08, 0x00, MS_OS_20_SUBSET_HEADER_CONFIGURATION, 0x00, 0x00, 0x00,
        MS_OS_20_SET_LEN - 10, 0x00,
        /* function subset: byte 22 is the first interface */
        0x08, 0x00, MS_OS_20_SUBSET_HEADER_FUNCTION, 0x00, 0x00, 0x00,
        MS_OS_20_SET_LEN - 18, 0x00,
        /* compatible ID */
        0x14, 0x00, MS_OS_20_FEATURE_COMPATBLE_ID, 0x00, 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(ms_os_20_set, set, sizeof set);
    ms_os_20_set[22] = itf;
}

static uint16_t dfu_rt_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                                  uint8_t *ep, uint8_t str)
{
    (void)self;
    (void)ep;
    build_ms_os_20(itf);
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

static const CH32RVUsbFunctionType dfu_rt_type = {&dfu_rt_driver, dfu_rt_descriptor, 1, CFG_TUD_DFU_RUNTIME};

static CH32RVUsbDeviceDfuRuntime *dfu;

CH32RVUsbDeviceDfuRuntime::CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, dfu_rt_type, "DFU runtime")
{
    if (attached()) {
        dfu = this;
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

extern "C" uint8_t const *tud_descriptor_bos_cb(void)
{
    return dfu ? bos : NULL;
}

/* The MS OS 2.0 descriptor set request: vendor, device, wIndex 7. Any other
 * vendor request is stalled. */
extern "C" bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                           tusb_control_request_t const *request)
{
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    if (dfu && request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == MS_OS_20_VENDOR_CODE && request->wIndex == 7) {
        return tud_control_xfer(rhport, request, ms_os_20_set, MS_OS_20_SET_LEN);
    }
    return false;
}

extern "C" void tud_dfu_runtime_reboot_to_dfu_cb(void)
{
    if (dfu) {
        dfu->detach();
    }
}

#else

static uint16_t dfu_rt_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType dfu_rt_type = {nullptr, dfu_rt_descriptor, 1, 1};

CH32RVUsbDeviceDfuRuntime::CH32RVUsbDeviceDfuRuntime(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, dfu_rt_type, "DFU runtime")
{
}

void CH32RVUsbDeviceDfuRuntime::detach() {}
void CH32RVUsbDeviceDfuRuntime::attachDefault(CH32RVUsbDevice &) {}

#endif
