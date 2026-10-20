/* DFU (DFU 1.1).
 *
 * Runtime: DFU_DETACH resets into the factory USB bootloader - or, on a part
 * whose program runs from RAM and can rewrite its own flash
 * (CH32RV_FLASH_SELF_WRITE: CH32H41x), the device comes back on the bus in
 * DFU mode instead, with this interface as its only function. DFU mode takes
 * a download (dfu-util -D image.bin) block by block into the code flash from
 * offset 0, erasing ahead of the writes, answers an upload (dfu-util -U) from
 * the flash, and resets once the download is complete. It is not
 * manifestation tolerant: the reset runs the new image.
 *
 * Windows has no in-box driver for a DFU interface; dfu-util (libusb) needs
 * WinUSB on it, which the Microsoft OS 2.0 set names
 * (CH32RVUsbDevicePlatform.cpp: compatible ID only, no DeviceInterfaceGUIDs -
 * libusb opens a WinUSB device without one). */
#include "CH32RVUsbDeviceInternal.h"

#include <ch32rv_flash.h>
#include <stdlib.h>
#include <string.h>

#define DFU_DETACH    0
#define DFU_DNLOAD    1
#define DFU_UPLOAD    2
#define DFU_GETSTATUS 3
#define DFU_CLRSTATUS 4
#define DFU_GETSTATE  5
#define DFU_ABORT     6

/* bState */
#define APP_IDLE           0
#define DFU_IDLE           2
#define DFU_DNLOAD_SYNC    3
#define DFU_DNLOAD_IDLE    5
#define DFU_MANIFEST_SYNC  6
#define DFU_MANIFEST       7
#define DFU_UPLOAD_IDLE    9
#define DFU_ERROR          10

/* bStatus */
#define ST_OK        0x00
#define ST_ERR_WRITE 0x03
#define ST_ERR_ERASE 0x04
#define ST_ERR_ADDR  0x08
#define ST_ERR_STALLEDPKT 0x0F

#define TRANSFER_SIZE 4096u

static bool dfu_rt_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                              uint8_t str, CH32RVUsbSpeed speed)
{
    (void)ep;
    (void)speed;
    const bool dfuMode = static_cast<const CH32RVUsbDeviceDfuRuntime &>(self).inDfuMode();
    /* bmAttributes: WILL_DETACH - the device leaves the bus by itself after
     * DFU_DETACH. Where it can write its own flash also CAN_DNLOAD and
     * CAN_UPLOAD (not manifestation tolerant); elsewhere what appears next is
     * the factory ISP bootloader, not a DFU-mode device, so no download is
     * claimed. Detach timeout 1000 ms, DFU 1.1. */
#if CH32RV_FLASH_SELF_WRITE
    const uint8_t attributes = 0x08 | 0x02 | 0x01;
    const uint16_t transfer = TRANSFER_SIZE;
#else
    const uint8_t attributes = 0x08;
    const uint16_t transfer = 64;
#endif
    const uint8_t d[] = {
        9, 4, itf, 0, 0, 0xFE, 0x01, (uint8_t)(dfuMode ? 0x02 : 0x01), str,
        9, 0x21, attributes, 0xE8, 0x03, (uint8_t)transfer, (uint8_t)(transfer >> 8), 0x10, 0x01,
    };
    w.put(d, sizeof d);
    return true;
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
#if CH32RV_FLASH_SELF_WRITE
    _enterDfu = true;               /* done from usbTask(): not inside the stack's callback */
    ch32rv_usb_deferred = [] {
        if (dfu) {
            dfu->usbTask();
        }
    };
#else
    CH32RVUsbDevice::requestBootloader();
#endif
}

/* The switch to DFU mode, from the device task (ch32rv_usb_deferred):
 * end() and begin() must not run inside a control transfer's callback. */
void CH32RVUsbDeviceDfuRuntime::usbTask()
{
#if CH32RV_FLASH_SELF_WRITE
    if (!_enterDfu) {
        return;
    }
    _enterDfu = false;
    if (!_block) {
        _block = (uint8_t *)malloc(TRANSFER_SIZE);
        if (!_block) {
            return;
        }
    }
    _dfuMode = true;
    setState(DFU_IDLE, ST_OK);
    if (!device().restartWithOnly(*this)) {
        CH32RVUsbDevice::enterBootloader();             /* nothing to come back as: reset */
    }
#endif
}

void CH32RVUsbDeviceDfuRuntime::setState(uint8_t state, uint8_t status)
{
    _status[0] = status;
    _status[4] = state;
}

#if CH32RV_FLASH_SELF_WRITE
/* One downloaded block at the running offset: erase what the block reaches
 * that is not erased yet, then program it padded to the write unit. */
bool CH32RVUsbDeviceDfuRuntime::writeBlock(uint16_t length)
{
    const uint32_t unit = ch32rv_flash_erase_size();
    const uint32_t padded = (length + CH32RV_FLASH_WRITE_SIZE - 1u) & ~(CH32RV_FLASH_WRITE_SIZE - 1u);
    if (_offset + padded > ch32rv_flash_size()) {
        setState(DFU_ERROR, ST_ERR_ADDR);
        return false;
    }
    while (_erased < _offset + padded) {
        if (!ch32rv_flash_erase(_erased, unit)) {
            setState(DFU_ERROR, ST_ERR_ERASE);
            return false;
        }
        _erased += unit;
    }
    memset(_block + length, 0xFF, padded - length);
    if (!ch32rv_flash_write(_offset, _block, padded)) {
        setState(DFU_ERROR, ST_ERR_WRITE);
        return false;
    }
    _offset += padded;
    return true;
}
#endif

bool CH32RVUsbDeviceDfuRuntime::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    const bool setup = stage == CH32RVUsbControlStage::Setup;
    switch (s.bRequest) {
    case DFU_DETACH:                             /* leave once the status stage is out */
        if (_dfuMode) {
            return false;
        }
        if (setup) {
            return device().controlStatus();
        }
        if (stage == CH32RVUsbControlStage::Ack) {
            detach();
        }
        return true;
    case DFU_GETSTATUS:
        if (setup) {
            const uint8_t state = _status[4];
            if (state == DFU_DNLOAD_SYNC) {
                setState(DFU_DNLOAD_IDLE, ST_OK);        /* the block was written in its data stage */
            } else if (state == DFU_MANIFEST_SYNC) {
                setState(DFU_MANIFEST, ST_OK);
            }
            return device().controlIn(_status, sizeof _status);
        }
        if (stage == CH32RVUsbControlStage::Ack && _status[4] == DFU_MANIFEST) {
            CH32RVUsbDevice::requestBootloader();        /* no bootloader here: a plain reset into the new image */
        }
        return true;
    case DFU_GETSTATE:
        return !setup || device().controlIn(_status + 4, 1);
#if CH32RV_FLASH_SELF_WRITE
    case DFU_CLRSTATUS:
    case DFU_ABORT:
        if (!_dfuMode) {
            return false;
        }
        if (setup) {
            setState(DFU_IDLE, ST_OK);
            return device().controlStatus();
        }
        return true;
    case DFU_DNLOAD: {
        if (!_dfuMode) {
            return false;
        }
        const uint8_t state = _status[4];
        if (setup) {
            if (state != DFU_IDLE && state != DFU_DNLOAD_IDLE) {
                setState(DFU_ERROR, ST_ERR_STALLEDPKT);
                return false;
            }
            if (state == DFU_IDLE) {                     /* a new download starts at offset 0 */
                _offset = 0;
                _erased = 0;
            }
            if (s.wLength == 0) {
                if (state == DFU_IDLE) {
                    setState(DFU_ERROR, ST_ERR_STALLEDPKT);
                    return false;
                }
                setState(DFU_MANIFEST_SYNC, ST_OK);      /* the end of the download */
                return device().controlStatus();
            }
            if (s.wLength > TRANSFER_SIZE) {
                setState(DFU_ERROR, ST_ERR_STALLEDPKT);
                return false;
            }
            return device().controlOut(_block, s.wLength);
        }
        if (stage == CH32RVUsbControlStage::Data) {
            if (writeBlock(s.wLength)) {
                setState(DFU_DNLOAD_SYNC, ST_OK);
            }
            return _status[4] != DFU_ERROR;
        }
        return true;
    }
    case DFU_UPLOAD: {
        if (!_dfuMode) {
            return false;
        }
        if (setup) {
            const uint8_t state = _status[4];
            if (state != DFU_IDLE && state != DFU_UPLOAD_IDLE) {
                setState(DFU_ERROR, ST_ERR_STALLEDPKT);
                return false;
            }
            if (state == DFU_IDLE) {
                _offset = 0;
            }
            const uint32_t size = ch32rv_flash_size();
            uint32_t n = s.wLength;
            if (n > size - _offset) {
                n = size - _offset;
            }
            /* Read at the flash's own address: 0x0 shows the system flash
             * when the BOOT area started us. */
            const uint8_t *src = (const uint8_t *)(uintptr_t)(0x08000000u + _offset);
            _offset += n;
            setState(n < s.wLength ? DFU_IDLE : DFU_UPLOAD_IDLE, ST_OK);
            return device().controlIn(src, (uint16_t)n);
        }
        return true;
    }
#endif
    }
    return false;
}
