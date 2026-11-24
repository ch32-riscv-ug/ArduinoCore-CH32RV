/* DFU download (CH32RVUsbDeviceDfu.h): DFU 1.1 in DFU mode, on EP0, into
 * the core's update engine (ch32rv_update.h).
 *
 * The state machine (DFU 1.1 figure A.1, not manifestation tolerant, will
 * detach): DNLOAD blocks are written in their data stage, so the GETSTATUS
 * after each finds the block done (dfuDNLOAD-SYNC -> dfuDNBUSY is never
 * needed: the host's poll comes after the write). The empty DNLOAD ends the
 * download; the GETSTATUS after it runs the checks (dfuMANIFEST-SYNC ->
 * dfuMANIFEST); once that status is out the device leaves the bus and
 * restarts into the image. */
#include "internal/CH32RVUsbDeviceInternal.h"

#include <internal/ch32rv_update.h>
#include <stdlib.h>

#define DFU_DNLOAD    1
#define DFU_GETSTATUS 3
#define DFU_CLRSTATUS 4
#define DFU_GETSTATE  5
#define DFU_ABORT     6

#define DFU_IDLE          2
#define DFU_DNLOAD_SYNC   3
#define DFU_DNLOAD_IDLE   5
#define DFU_MANIFEST_SYNC 6
#define DFU_MANIFEST      7
#define DFU_ERROR         10

static bool dfu_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                           uint8_t str, CH32RVUsbSpeed speed)
{
    (void)ep;
    (void)speed;
    const uint16_t transfer = static_cast<const CH32RVUsbDeviceDfu &>(self).configuration().transferBytes;
    /* DFU mode (protocol 2); bmAttributes CAN_DNLOAD | WILL_DETACH: not
     * manifestation tolerant, no upload. Detach timeout 1000 ms, DFU 1.1. */
    const uint8_t d[] = {
        9, 4, itf, 0, 0, 0xFE, 0x01, 0x02, str,
        9, 0x21, 0x09, 0xE8, 0x03, (uint8_t)transfer, (uint8_t)(transfer >> 8), 0x10, 0x01,
    };
    w.put(d, sizeof d);
    return true;
}

static const CH32RVUsbFunctionType dfu_type = {dfu_descriptor, 1, 1, nullptr, 1};

CH32RVUsbDeviceDfu::CH32RVUsbDeviceDfu(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceFunction(device, dfu_type, name ? name : CH32RV_USB_DFU_NAME)
{
    if (attached()) {
        ch32rv_usb_platform_use();         /* WinUSB for dfu-util on Windows (MS OS 2.0) */
    }
}

bool CH32RVUsbDeviceDfu::configure(const CH32RVUsbDfuConfig &config)
{
    if (device().started() || config.transferBytes < 64 || config.transferBytes > 4096) {
        return false;
    }
    if (_ownBlock && _block != config.transferBuffer) {
        free(_block);
        _ownBlock = false;
    }
    _block = config.transferBuffer;
    _config = config;
    return true;
}

bool CH32RVUsbDeviceDfu::downloading() const
{
    return _status[4] == DFU_DNLOAD_IDLE || _status[4] == DFU_DNLOAD_SYNC || _status[4] == DFU_MANIFEST_SYNC;
}

void CH32RVUsbDeviceDfu::setState(uint8_t state, CH32RVUsbDfuStatus status, uint32_t pollMs)
{
    _status[0] = (uint8_t)status;
    _status[1] = (uint8_t)pollMs;
    _status[2] = (uint8_t)(pollMs >> 8);
    _status[3] = (uint8_t)(pollMs >> 16);
    _status[4] = state;
}

void CH32RVUsbDeviceDfu::failed(CH32RVUsbDfuStatus status)
{
    ch32rv_update_abort();
    setState(DFU_ERROR, status);
    if (_onError) {
        _onError(status);
    }
}

/* The engine's error as the DFU status the host reads. */
static CH32RVUsbDfuStatus status_of(uint8_t error)
{
    switch (error) {
    case CH32RV_UPDATE_ERASE:
        return CH32RVUsbDfuStatus::Erase;
    case CH32RV_UPDATE_SPACE:
    case CH32RV_UPDATE_NO_SPACE:
        return CH32RVUsbDfuStatus::Address;
    case CH32RV_UPDATE_VERIFY:
        return CH32RVUsbDfuStatus::Verify;
    default:
        return CH32RVUsbDfuStatus::Write;           /* not an image, or a page that did not take */
    }
}

/* One block, in its data stage. The first starts the update. */
bool CH32RVUsbDeviceDfu::block(uint16_t length)
{
    if (_status[4] == DFU_IDLE && !ch32rv_update_begin(CH32RV_UPDATE_SIZE_UNKNOWN, false)) {
        failed(status_of(ch32rv_update_error()));
        return false;
    }
    if (!ch32rv_update_write(_block, length)) {
        failed(status_of(ch32rv_update_error()));
        return false;
    }
    setState(DFU_DNLOAD_SYNC, CH32RVUsbDfuStatus::Ok, _config.downloadPollMs);
    if (_onProgress) {
        _onProgress(ch32rv_update_progress());
    }
    return true;
}

/* The GETSTATUS after the empty DNLOAD: the checks. */
void CH32RVUsbDeviceDfu::manifest()
{
    if (!ch32rv_update_end(true)) {
        failed(CH32RVUsbDfuStatus::Verify);
        return;
    }
    if (_onComplete && !_onComplete()) {
        ch32rv_update_cancel_pending();
        failed(CH32RVUsbDfuStatus::Verify);
        return;
    }
    setState(DFU_MANIFEST, CH32RVUsbDfuStatus::Ok, _config.manifestPollMs);
}

void CH32RVUsbDeviceDfu::usbReset()
{
    /* A bus reset in the middle of a download abandons it (DFU 1.1: to
     * dfuIDLE, the image unused); after manifestation the device is leaving. */
    if (_status[4] != DFU_MANIFEST) {
        if (ch32rv_update_running()) {
            ch32rv_update_abort();
        }
        setState(DFU_IDLE, CH32RVUsbDfuStatus::Ok);
    }
}

bool CH32RVUsbDeviceDfu::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    const bool setup = stage == CH32RVUsbControlStage::Setup;
    const uint8_t state = _status[4];
    switch (s.bRequest) {
    case DFU_GETSTATUS:
        if (setup) {
            if (state == DFU_DNLOAD_SYNC) {
                setState(DFU_DNLOAD_IDLE, CH32RVUsbDfuStatus::Ok, _config.downloadPollMs);
            } else if (state == DFU_MANIFEST_SYNC) {
                manifest();
            }
            return device().controlIn(_status, sizeof _status);
        }
        if (stage == CH32RVUsbControlStage::Ack && _status[4] == DFU_MANIFEST && _restart) {
            ch32rv_usb_install_after(device(), 20);
        }
        return true;
    case DFU_GETSTATE:
        return !setup || device().controlIn(_status + 4, 1);
    case DFU_CLRSTATUS:
    case DFU_ABORT:
        if (setup) {
            if (state == DFU_MANIFEST) {
                return false;                     /* accepted: leaving */
            }
            if (ch32rv_update_running()) {
                ch32rv_update_abort();
            }
            setState(DFU_IDLE, CH32RVUsbDfuStatus::Ok);
            return device().controlStatus();
        }
        return true;
    case DFU_DNLOAD:
        if (setup) {
            if (state != DFU_IDLE && state != DFU_DNLOAD_IDLE) {
                failed(CH32RVUsbDfuStatus::StalledPacket);
                return false;
            }
            if (s.wLength == 0) {
                if (state == DFU_IDLE) {
                    failed(CH32RVUsbDfuStatus::StalledPacket);
                    return false;
                }
                setState(DFU_MANIFEST_SYNC, CH32RVUsbDfuStatus::Ok, _config.manifestPollMs);
                return device().controlStatus();
            }
            if (s.wLength > _config.transferBytes) {
                failed(CH32RVUsbDfuStatus::StalledPacket);
                return false;
            }
            if (!_block) {
                _block = (uint8_t *)malloc(_config.transferBytes);
                _ownBlock = _block != nullptr;
                if (!_block) {
                    failed(CH32RVUsbDfuStatus::Unknown);
                    return false;
                }
            }
            return device().controlOut(_block, s.wLength);
        }
        if (stage == CH32RVUsbControlStage::Data) {
            /* A failed block is a status the next GETSTATUS reads, not a stall. */
            block(s.wLength);
        }
        return true;
    }
    return false;
}
