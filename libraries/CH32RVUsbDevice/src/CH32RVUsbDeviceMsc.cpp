/* USB mass storage: TinyUSB's msc_device (bulk-only transport, SCSI), one
 * logical unit. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

static void copy_padded(uint8_t *dst, size_t size, const char *value)
{
    memset(dst, 0, size);
    if (value) {
        size_t n = strlen(value);
        memcpy(dst, value, n > size ? size : n);
    }
}

void CH32RVUsbDeviceMsc::vendorID(const char *value)
{
    copy_padded((uint8_t *)_vendor, sizeof _vendor - 1, value);
}

void CH32RVUsbDeviceMsc::productID(const char *value)
{
    copy_padded((uint8_t *)_product, sizeof _product - 1, value);
}

void CH32RVUsbDeviceMsc::productRevision(const char *value)
{
    copy_padded((uint8_t *)_revision, sizeof _revision - 1, value);
}

bool CH32RVUsbDeviceMsc::begin(uint32_t blockCount, uint16_t blockSize)
{
    _blockCount = blockCount;
    _blockSize = blockSize;
    return begin();
}

void CH32RVUsbDeviceMsc::end()
{
    _mediaPresent = false;
}

void CH32RVUsbDeviceMsc::inquiry(uint8_t vendor[8], uint8_t product[16], uint8_t revision[4]) const
{
    copy_padded(vendor, 8, _vendor);
    copy_padded(product, 16, _product);
    copy_padded(revision, 4, _revision);
}

void CH32RVUsbDeviceMsc::capacity(uint32_t *blockCount, uint16_t *blockSize) const
{
    *blockCount = _mediaPresent ? _blockCount : 0;
    *blockSize = _mediaPresent ? _blockSize : 0;
}

bool CH32RVUsbDeviceMsc::startStop(uint8_t powerCondition, bool start, bool loadEject)
{
    bool ok = true;
    if (_storage) {
        ok = _storage->startStop(powerCondition, start, loadEject);
    }
    if (_onStartStop) {
        ok = _onStartStop(powerCondition, start, loadEject) && ok;
    }
    return ok;
}

int32_t CH32RVUsbDeviceMsc::read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (!_mediaPresent) {
        return -1;
    }
    if (_onRead) {
        return _onRead(lba, offset, buffer, size);
    }
    return _storage ? _storage->read(lba, offset, buffer, size) : -1;
}

int32_t CH32RVUsbDeviceMsc::write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (!_mediaPresent || !_writable) {
        return -1;
    }
    if (_onWrite) {
        return _onWrite(lba, offset, buffer, size);
    }
    return _storage ? _storage->write(lba, offset, buffer, size) : -1;
}

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t msc_driver = {
    NULL, mscd_init, mscd_deinit, mscd_reset, mscd_open, mscd_control_xfer_cb, mscd_xfer_cb, NULL, NULL,
};

static CH32RVUsbDeviceMsc *msc;

static uint16_t msc_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                               uint8_t *ep, uint8_t str, bool hs)
{
    (void)self;
    const uint8_t n = (*ep)++;
    const uint8_t d[] = {TUD_MSC_DESCRIPTOR(itf, str, n, (uint8_t)(0x80 | n), ch32rv_usb_bulk_size(hs))};
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType msc_type = {&msc_driver, msc_descriptor, 1, CFG_TUD_MSC, nullptr, 0};

CH32RVUsbDeviceMsc::CH32RVUsbDeviceMsc(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, msc_type, CH32RV_USB_MSC_NAME)
{
    if (attached()) {
        msc = this;
    }
}

extern "C" uint8_t tud_msc_get_maxlun_cb(void)
{
    return 1;
}

extern "C" void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16],
                                   uint8_t product_rev[4])
{
    (void)lun;
    if (msc) {
        msc->inquiry(vendor_id, product_id, product_rev);
    }
}

extern "C" bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (msc && msc->testUnitReady()) {
        return true;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);   /* medium not present */
    return false;
}

extern "C" void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = 0;
    *block_size = 0;
    if (msc) {
        msc->capacity(block_count, block_size);
    }
}

extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    return msc ? msc->startStop(power_condition, start, load_eject) : true;
}

extern "C" int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    return msc ? msc->read10(lba, offset, buffer, bufsize) : -1;
}

extern "C" int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer,
                                      uint32_t bufsize)
{
    (void)lun;
    return msc ? msc->write10(lba, offset, buffer, bufsize) : -1;
}

extern "C" bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return msc && msc->writable();
}

/* The SCSI commands TinyUSB leaves to the application: accept the two a
 * host sends around an eject and a cache flush, reject the rest. */
extern "C" int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;
    if (scsi_cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL || scsi_cmd[0] == 0x35 /* SYNCHRONIZE CACHE (10) */) {
        return 0;
    }
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

#else

static uint16_t msc_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t, bool)
{
    return 0;
}

static const CH32RVUsbFunctionType msc_type = {nullptr, msc_descriptor, 1, 1, nullptr, 0};

CH32RVUsbDeviceMsc::CH32RVUsbDeviceMsc(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, msc_type, CH32RV_USB_MSC_NAME)
{
}

#endif
