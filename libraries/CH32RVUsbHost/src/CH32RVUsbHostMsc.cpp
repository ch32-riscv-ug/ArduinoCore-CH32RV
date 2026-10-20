/* CH32RVUsbHost: mass storage, bulk-only SCSI (TinyUSB's MSC host driver).
 * Every call waits for its command, driving the stack while it does. One
 * device at a time is addressed; the LUN is chosen with mscSelectLun(). */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if 0 /* moving to the own host stack */

namespace {

const usbh_class_driver_t msc_driver = {
    "MSC", msch_init, msch_deinit, msch_open, msch_set_config, msch_xfer_cb, msch_close,
};

void poll(CH32RVUsbHost &host);
CH32RVUsbHostGroup msc_group = {&msc_driver, poll, nullptr, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(msc_group); }
} join;

uint8_t lun_of[CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1];
bool claim_due[CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1];
CH32RVUsbHostMscSense last_sense;

/* One command at a time. Static so that a command given up on still lands
 * in valid memory when it completes. */
struct Wait {
    volatile bool done;
    volatile uint8_t status;     /* CSW status: 0 passed, 1 failed, 2 phase error */
    uint32_t generation;
};
Wait wait_state;
TU_ATTR_ALIGNED(4) uint8_t small_buf[36];

bool complete(uint8_t daddr, const tuh_msc_complete_data_t *data)
{
    (void)daddr;
    if (data->user_arg != wait_state.generation) {
        return true;
    }
    wait_state.status = data->csw ? data->csw->status : 0xFF;
    wait_state.done = true;
    return true;
}

uint8_t device(uint8_t address)
{
    for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX + CFG_TUH_HUB; a++) {
        if (tuh_msc_mounted(a) && (address == CH32RV_USB_HOST_ANY_ADDRESS || a == address)) {
            return a;
        }
    }
    return 0;
}

uint32_t arm()
{
    wait_state.generation++;
    wait_state.done = false;
    wait_state.status = 0xFF;
    return wait_state.generation;
}

/* A command the device never finished: TinyUSB's MSC driver cannot abandon
 * one (it would wait forever, and could still write the caller's buffer). Stop
 * the bulk endpoints and have the device enumerate again - for the sketch, the
 * drive was unplugged and plugged back in. */
void recover(uint8_t daddr)
{
    const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
    const uint8_t *cfg = ch32rv_usb_host_scratch;
    bool here = false;
    for (uint16_t at = 0; at + 2 <= len && cfg[at] >= 2 && at + cfg[at] <= len; at = (uint16_t)(at + cfg[at])) {
        if (cfg[at + 1] == TUSB_DESC_INTERFACE) {
            here = cfg[at + 5] == TUSB_CLASS_MSC;
        } else if (here && cfg[at + 1] == TUSB_DESC_ENDPOINT) {
            tuh_edpt_abort_xfer(daddr, cfg[at + 2]);
        }
    }
    ch32rv_hcd_usbfs_reenumerate(CH32RV_TUH_RHPORT);
}

uint8_t current_daddr;     /* the device of the command in flight */

/* Wait for the command `started` launched. */
bool finish(CH32RVUsbHost &host, bool started, uint32_t timeoutMs)
{
    if (!started) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    if (!ch32rv_usb_host_wait(wait_state.done, timeoutMs)) {
        wait_state.generation++;
        recover(current_daddr);
        return host.fail(CH32RVUsbHostError::Timeout);
    }
    if (wait_state.status != 0) {
        return host.fail(wait_state.status == 1 ? CH32RVUsbHostError::Stalled
                                                : CH32RVUsbHostError::TransferFailed);
    }
    return host.ok();
}

/* Wait for the bus to be free of a previous command (TinyUSB runs one per
 * device). */
bool idle(CH32RVUsbHost &host, uint8_t daddr, uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (!tuh_msc_ready(daddr)) {
        tuh_task_ext(0, false);
        if (!tuh_msc_mounted(daddr)) {
            return host.fail(CH32RVUsbHostError::NotConnected);
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return host.fail(CH32RVUsbHostError::Timeout);
        }
    }
    return true;
}

/* TinyUSB keeps the MSC interface number to itself; look it up for
 * getInterfaces()'s "claimed". */
void poll(CH32RVUsbHost &host)
{
    (void)host;
    for (uint8_t daddr = 1; daddr < sizeof claim_due; daddr++) {
        if (!claim_due[daddr]) {
            continue;
        }
        claim_due[daddr] = false;
        const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
        const uint8_t *cfg = ch32rv_usb_host_scratch;
        for (uint16_t at = 0; at + 9 <= len && cfg[at] >= 2; at = (uint16_t)(at + cfg[at])) {
            if (cfg[at + 1] == TUSB_DESC_INTERFACE && cfg[at + 5] == TUSB_CLASS_MSC) {
                ch32rv_usb_host_mark_claimed(daddr, cfg[at + 2]);
                break;
            }
        }
    }
}

}  // namespace

extern "C" void tuh_msc_mount_cb(uint8_t daddr)
{
    if (daddr < sizeof lun_of) {
        lun_of[daddr] = 0;
        claim_due[daddr] = true;     /* after enumeration, from the task */
    }
}

extern "C" void tuh_msc_umount_cb(uint8_t daddr)
{
    (void)daddr;
}

bool CH32RVUsbHost::mscReady(uint8_t address) const
{
    return device(address) != 0;
}

bool CH32RVUsbHost::mscMaxLun(uint8_t &maxLun, uint8_t address)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    maxLun = (uint8_t)(tuh_msc_get_maxlun(d) - 1u);
    return ok();
}

bool CH32RVUsbHost::mscSelectLun(uint8_t lun, uint8_t address)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (lun >= tuh_msc_get_maxlun(d)) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    lun_of[d] = lun;
    return ok();
}

bool CH32RVUsbHost::mscInquiry(CH32RVUsbHostMscInquiry &inquiry, uint8_t address, uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    scsi_inquiry_resp_t *r = (scsi_inquiry_resp_t *)(void *)small_buf;
    const uint32_t g = arm();
    if (!finish(*this, tuh_msc_inquiry(d, lun_of[d], r, complete, g), timeoutMs)) {
        return false;
    }
    inquiry = CH32RVUsbHostMscInquiry();
    inquiry.peripheralDeviceType = small_buf[0] & 0x1F;
    inquiry.removable = small_buf[1] & 0x80;
    memcpy(inquiry.vendor, r->vendor_id, 8);
    memcpy(inquiry.product, r->product_id, 16);
    memcpy(inquiry.revision, r->product_rev, 4);
    return true;
}

bool CH32RVUsbHost::mscRequestSense(CH32RVUsbHostMscSense &sense, uint8_t address, uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    memset(small_buf, 0, 18);
    const uint32_t g = arm();
    if (!finish(*this, tuh_msc_request_sense(d, lun_of[d], small_buf, complete, g), timeoutMs)) {
        return false;
    }
    sense = CH32RVUsbHostMscSense();
    sense.responseCode = small_buf[0] & 0x7F;
    sense.senseKey = small_buf[2] & 0x0F;
    sense.additionalSenseCode = small_buf[12];
    sense.additionalSenseQualifier = small_buf[13];
    last_sense = sense;
    return true;
}

bool CH32RVUsbHost::mscTestUnitReady(uint8_t address, uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    const uint32_t g = arm();
    return finish(*this, tuh_msc_test_unit_ready(d, lun_of[d], complete, g), timeoutMs);
}

bool CH32RVUsbHost::mscWaitReady(uint8_t address, uint32_t readyTimeoutMs)
{
    const uint32_t t0 = millis();
    for (;;) {
        if (mscTestUnitReady(address, 1000)) {
            return true;
        }
        if (_error == CH32RVUsbHostError::NotConnected) {
            return false;
        }
        CH32RVUsbHostMscSense s;
        mscRequestSense(s, address, 1000);
        if ((uint32_t)(millis() - t0) >= readyTimeoutMs) {
            return fail(CH32RVUsbHostError::Timeout);
        }
        delay(10);
    }
}

bool CH32RVUsbHost::mscCapacity(uint32_t &blockCount, uint32_t &blockSize, uint8_t address, uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    scsi_read_capacity10_resp_t *r = (scsi_read_capacity10_resp_t *)(void *)small_buf;
    const uint32_t g = arm();
    if (!finish(*this, tuh_msc_read_capacity(d, lun_of[d], r, complete, g), timeoutMs)) {
        return false;
    }
    blockCount = tu_ntohl(r->last_lba) + 1u;
    blockSize = tu_ntohl(r->block_size);
    return true;
}

bool CH32RVUsbHost::mscGetBlockDeviceInfo(CH32RVUsbHostMscBlockDeviceInfo &info, uint8_t address)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    info = CH32RVUsbHostMscBlockDeviceInfo();
    info.address = d;
    info.lun = lun_of[d];
    info.maxLun = (uint8_t)(tuh_msc_get_maxlun(d) - 1u);
    info.blockCount = tuh_msc_get_block_count(d, info.lun);
    info.blockSize = tuh_msc_get_block_size(d, info.lun);
    info.capacityBytes = (uint64_t)info.blockCount * info.blockSize;
    return ok();
}

bool CH32RVUsbHost::mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount, uint8_t address,
                                  uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!data || !blockCount || blockCount > 0xFFFFu) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    const uint32_t g = arm();
    return finish(*this, tuh_msc_read10(d, lun_of[d], data, lba, (uint16_t)blockCount, complete, g), timeoutMs);
}

bool CH32RVUsbHost::mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount, uint8_t address,
                                   uint32_t timeoutMs)
{
    const uint8_t d = device(address);
    if (!d) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    current_daddr = d;
    if (!data || !blockCount || blockCount > 0xFFFFu) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    if (!idle(*this, d, timeoutMs)) {
        return false;
    }
    const uint32_t g = arm();
    return finish(*this, tuh_msc_write10(d, lun_of[d], data, lba, (uint16_t)blockCount, complete, g), timeoutMs);
}

#else /* no host */

bool CH32RVUsbHost::mscReady(uint8_t) const { return false; }
bool CH32RVUsbHost::mscInquiry(CH32RVUsbHostMscInquiry &, uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscRequestSense(CH32RVUsbHostMscSense &, uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscMaxLun(uint8_t &, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscSelectLun(uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscTestUnitReady(uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscWaitReady(uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscCapacity(uint32_t &, uint32_t &, uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::mscGetBlockDeviceInfo(CH32RVUsbHostMscBlockDeviceInfo &, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::mscReadBlocks(uint32_t, uint8_t *, uint32_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::mscWriteBlocks(uint32_t, const uint8_t *, uint32_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}

#endif
