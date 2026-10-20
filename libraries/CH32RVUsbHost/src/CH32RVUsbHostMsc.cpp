/* CH32RVUsbHost: mass storage, bulk-only transport and SCSI. Every call
 * waits for its command, driving the stack while it does. One device at a
 * time is addressed; the LUN is chosen with mscSelectLun(). */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

struct Msc {
    bool mounted;
    uint8_t daddr, itf, epIn, epOut;
    uint8_t maxLun, lun;
    uint32_t tag;
    uint32_t blockCount, blockSize;       /* READ CAPACITY at mount, 0 when no medium */
};
Msc mscs[1];
CH32RVUsbHostMscSense last_sense;
uint8_t small_buf[36] __attribute__((aligned(4)));
uint8_t cbw[31] __attribute__((aligned(4)));
uint8_t csw[13] __attribute__((aligned(4)));

uint16_t msc_open(uint8_t daddr, const uint8_t *itf, uint16_t length);
void msc_configured(uint8_t daddr, uint8_t itf);
void msc_detach(uint8_t daddr);
CH32RVUsbHostGroup msc_group = {msc_open, msc_configured, nullptr, msc_detach, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(msc_group); }
} join;

/* Mass storage, SCSI transparent, bulk-only: its two bulk endpoints. */
uint16_t msc_open(uint8_t daddr, const uint8_t *itf, uint16_t length)
{
    if (itf[5] != 0x08 || itf[6] != 0x06 || itf[7] != 0x50 || mscs[0].daddr) {
        return 0;
    }
    Msc m = {};
    uint16_t at = itf[0];
    while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4) {
        const uint8_t *d = itf + at;
        if (d[1] == 5 && d[0] >= 7 && (d[3] & 3) == 2) {
            if ((d[2] & 0x80) && !m.epIn && ch32rv_usb_host_ep_open(daddr, d)) {
                m.epIn = d[2];
            } else if (!(d[2] & 0x80) && !m.epOut && ch32rv_usb_host_ep_open(daddr, d)) {
                m.epOut = d[2];
            }
        }
        at = (uint16_t)(at + d[0]);
    }
    if (!m.epIn || !m.epOut) {
        return 0;
    }
    m.daddr = daddr;
    m.itf = itf[2];
    mscs[0] = m;
    return at;
}

void msc_detach(uint8_t daddr)
{
    if (mscs[0].daddr == daddr) {
        memset(&mscs[0], 0, sizeof mscs[0]);
    }
}

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

/* Reset recovery (bulk-only transport 5.3.4): the class reset, then both
 * halts cleared. */
bool reset_recovery(Msc &m)
{
    ch32rv_usb_host_abort(m.daddr, m.epIn);
    ch32rv_usb_host_abort(m.daddr, m.epOut);
    const bool ok = ch32rv_usb_host_control_sync(m.daddr, 0x21, 0xFF, 0, m.itf, nullptr, 0, nullptr, 1000) ==
                    CH32RV_USBH_OK;
    ch32rv_usb_host_clear_halt(m.daddr, m.epIn);
    ch32rv_usb_host_clear_halt(m.daddr, m.epOut);
    return ok;
}

/* One command: the block, its data, the status. The CSW status (0 passed, 1
 * failed, 2 phase error) in *status; the return value the transport's. */
CH32RVUsbHostError command(Msc &m, const uint8_t *cdb, uint8_t cdbLength, uint8_t *data, uint32_t length, bool in,
                           uint32_t timeoutMs, uint8_t *status)
{
    *status = 0xFF;
    const uint32_t tag = ++m.tag;
    memset(cbw, 0, sizeof cbw);
    const uint8_t head[15] = {'U', 'S', 'B', 'C', (uint8_t)tag, (uint8_t)(tag >> 8), (uint8_t)(tag >> 16),
                              (uint8_t)(tag >> 24), (uint8_t)length, (uint8_t)(length >> 8),
                              (uint8_t)(length >> 16), (uint8_t)(length >> 24), (uint8_t)(in ? 0x80 : 0),
                              m.lun, cdbLength};
    memcpy(cbw, head, sizeof head);
    memcpy(cbw + 15, cdb, cdbLength);
    uint8_t r = ch32rv_usb_host_xfer_sync(m.daddr, m.epOut, cbw, sizeof cbw, nullptr, timeoutMs);
    if (r == CH32RV_USBH_STALLED) {
        reset_recovery(m);
        return CH32RVUsbHostError::Stalled;
    }
    if (r != CH32RV_USBH_OK) {
        if (r == CH32RV_USBH_TIMEOUT) {
            reset_recovery(m);
        }
        return ch32rv_usb_host_result_error(r);
    }
    /* The data stage in pieces the controller's transfer length takes; the
     * device sees one stream. A short packet ends it early. */
    uint32_t done = 0;
    while (done < length) {
        const uint32_t left = length - done;
        const uint16_t n = (uint16_t)(left < 16384u ? left : 16384u);
        uint16_t got = 0;
        r = ch32rv_usb_host_xfer_sync(m.daddr, in ? m.epIn : m.epOut, data + done, n, &got, timeoutMs);
        if (r == CH32RV_USBH_STALLED) {
            ch32rv_usb_host_clear_halt(m.daddr, in ? m.epIn : m.epOut);
            break;                     /* the status tells why */
        }
        if (r != CH32RV_USBH_OK) {
            if (r == CH32RV_USBH_TIMEOUT) {
                reset_recovery(m);
            }
            return ch32rv_usb_host_result_error(r);
        }
        done += got;
        if (got < n) {
            break;
        }
    }
    uint16_t got = 0;
    r = ch32rv_usb_host_xfer_sync(m.daddr, m.epIn, csw, sizeof csw, &got, timeoutMs);
    if (r == CH32RV_USBH_STALLED) {   /* once more after clearing the halt (5.3.3) */
        ch32rv_usb_host_clear_halt(m.daddr, m.epIn);
        r = ch32rv_usb_host_xfer_sync(m.daddr, m.epIn, csw, sizeof csw, &got, timeoutMs);
    }
    if (r != CH32RV_USBH_OK) {
        reset_recovery(m);
        return ch32rv_usb_host_result_error(r);
    }
    const uint32_t ctag = (uint32_t)csw[4] | (uint32_t)csw[5] << 8 | (uint32_t)csw[6] << 16 | (uint32_t)csw[7] << 24;
    if (got != 13 || memcmp(csw, "USBS", 4) != 0 || ctag != tag || csw[12] > 2) {
        reset_recovery(m);
        return CH32RVUsbHostError::InvalidResponse;
    }
    *status = csw[12];
    if (csw[12] == 2) {
        reset_recovery(m);             /* phase error */
    }
    return CH32RVUsbHostError::None;
}

/* The transport and the status as one error. */
CH32RVUsbHostError run(Msc &m, const uint8_t *cdb, uint8_t cdbLength, uint8_t *data, uint32_t length, bool in,
                       uint32_t timeoutMs)
{
    uint8_t status = 0xFF;
    const CH32RVUsbHostError e = command(m, cdb, cdbLength, data, length, in, timeoutMs, &status);
    if (e != CH32RVUsbHostError::None) {
        return e;
    }
    return status == 0 ? CH32RVUsbHostError::None
           : status == 1 ? CH32RVUsbHostError::Stalled
                         : CH32RVUsbHostError::TransferFailed;
}

bool read_capacity(Msc &m, uint32_t timeoutMs)
{
    const uint8_t cdb[10] = {0x25};
    if (run(m, cdb, 10, small_buf, 8, true, timeoutMs) != CH32RVUsbHostError::None) {
        return false;
    }
    m.blockCount = be32(small_buf) + 1u;
    m.blockSize = be32(small_buf + 4);
    return true;
}

/* After SET_CONFIGURATION: how many units, and the first one's geometry.
 * A drive without a medium answers TEST UNIT READY with "not ready" for as
 * long as it has none; a few tries, then it is mounted without a capacity
 * (the sketch's mscWaitReady() can wait longer). */
void msc_configured(uint8_t daddr, uint8_t itf)
{
    Msc &m = mscs[0];
    if (m.daddr != daddr || m.itf != itf || m.mounted) {
        return;
    }
    uint8_t lun = 0;
    uint16_t got = 0;
    if (ch32rv_usb_host_control_sync(daddr, 0xA1, 0xFE, 0, itf, &lun, 1, &got, 1000) != CH32RV_USBH_OK || !got) {
        lun = 0;                       /* a STALL means one unit (5.3.3) */
    }
    m.maxLun = lun > 15 ? 15 : lun;
    m.lun = 0;
    m.mounted = true;
    ch32rv_usb_host_mark_claimed(daddr, itf);
    const uint8_t tur[6] = {0x00};
    for (uint8_t i = 0; i < 3 && ch32rv_usb_host_connected(daddr); i++) {
        if (run(m, tur, 6, nullptr, 0, false, 1000) == CH32RVUsbHostError::None) {
            read_capacity(m, 1000);
            return;
        }
        const uint8_t sense[6] = {0x03, 0, 0, 0, 18, 0};
        run(m, sense, 6, small_buf, 18, true, 1000);
    }
}

Msc *device(uint8_t address)
{
    Msc &m = mscs[0];
    return m.mounted && ch32rv_usb_host_connected(m.daddr) &&
                   (address == CH32RV_USB_HOST_ANY_ADDRESS || m.daddr == address)
               ? &m
               : nullptr;
}

}  // namespace

bool CH32RVUsbHost::mscReady(uint8_t address) const
{
    return device(address) != nullptr;
}

bool CH32RVUsbHost::mscMaxLun(uint8_t &maxLun, uint8_t address)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    maxLun = m->maxLun;
    return ok();
}

bool CH32RVUsbHost::mscSelectLun(uint8_t lun, uint8_t address)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (lun > m->maxLun) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    if (lun != m->lun) {
        m->lun = lun;
        m->blockCount = m->blockSize = 0;
        read_capacity(*m, 1000);
    }
    return ok();
}

bool CH32RVUsbHost::mscInquiry(CH32RVUsbHostMscInquiry &inquiry, uint8_t address, uint32_t timeoutMs)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    const uint8_t cdb[6] = {0x12, 0, 0, 0, 36, 0};
    memset(small_buf, 0, 36);
    const CH32RVUsbHostError e = run(*m, cdb, 6, small_buf, 36, true, timeoutMs);
    if (e != CH32RVUsbHostError::None) {
        return fail(e);
    }
    inquiry = CH32RVUsbHostMscInquiry();
    inquiry.peripheralDeviceType = small_buf[0] & 0x1F;
    inquiry.removable = small_buf[1] & 0x80;
    memcpy(inquiry.vendor, small_buf + 8, 8);
    memcpy(inquiry.product, small_buf + 16, 16);
    memcpy(inquiry.revision, small_buf + 32, 4);
    return ok();
}

bool CH32RVUsbHost::mscRequestSense(CH32RVUsbHostMscSense &sense, uint8_t address, uint32_t timeoutMs)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    const uint8_t cdb[6] = {0x03, 0, 0, 0, 18, 0};
    memset(small_buf, 0, 18);
    const CH32RVUsbHostError e = run(*m, cdb, 6, small_buf, 18, true, timeoutMs);
    if (e != CH32RVUsbHostError::None) {
        return fail(e);
    }
    sense = CH32RVUsbHostMscSense();
    sense.responseCode = small_buf[0] & 0x7F;
    sense.senseKey = small_buf[2] & 0x0F;
    sense.additionalSenseCode = small_buf[12];
    sense.additionalSenseQualifier = small_buf[13];
    last_sense = sense;
    return ok();
}

bool CH32RVUsbHost::mscTestUnitReady(uint8_t address, uint32_t timeoutMs)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    const uint8_t cdb[6] = {0x00};
    const CH32RVUsbHostError e = run(*m, cdb, 6, nullptr, 0, false, timeoutMs);
    if (e != CH32RVUsbHostError::None) {
        return fail(e);
    }
    if (!m->blockSize) {
        read_capacity(*m, timeoutMs);  /* a medium arrived */
    }
    return ok();
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
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    const uint8_t cdb[10] = {0x25};
    const CH32RVUsbHostError e = run(*m, cdb, 10, small_buf, 8, true, timeoutMs);
    if (e != CH32RVUsbHostError::None) {
        return fail(e);
    }
    m->blockCount = blockCount = be32(small_buf) + 1u;
    m->blockSize = blockSize = be32(small_buf + 4);
    return ok();
}

bool CH32RVUsbHost::mscGetBlockDeviceInfo(CH32RVUsbHostMscBlockDeviceInfo &info, uint8_t address)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    info = CH32RVUsbHostMscBlockDeviceInfo();
    info.address = m->daddr;
    info.interfaceNumber = m->itf;
    info.lun = m->lun;
    info.maxLun = m->maxLun;
    info.blockCount = m->blockCount;
    info.blockSize = m->blockSize;
    info.capacityBytes = (uint64_t)info.blockCount * info.blockSize;
    return ok();
}

static bool rw10(CH32RVUsbHost &host, Msc *m, bool write, uint32_t lba, uint8_t *data, uint32_t blockCount,
                 uint32_t timeoutMs)
{
    if (!m->blockSize) {
        read_capacity(*m, timeoutMs);
    }
    if (!m->blockSize) {
        return host.fail(CH32RVUsbHostError::NotConnected);   /* no medium */
    }
    const uint8_t cdb[10] = {(uint8_t)(write ? 0x2A : 0x28), 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16),
                             (uint8_t)(lba >> 8), (uint8_t)lba, 0, (uint8_t)(blockCount >> 8),
                             (uint8_t)blockCount, 0};
    const CH32RVUsbHostError e = run(*m, cdb, 10, data, blockCount * m->blockSize, !write, timeoutMs);
    return e == CH32RVUsbHostError::None ? host.ok() : host.fail(e);
}

bool CH32RVUsbHost::mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount, uint8_t address,
                                  uint32_t timeoutMs)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (!data || !blockCount || blockCount > 0xFFFFu) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    return rw10(*this, m, false, lba, data, blockCount, timeoutMs);
}

bool CH32RVUsbHost::mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount, uint8_t address,
                                   uint32_t timeoutMs)
{
    Msc *m = device(address);
    if (!m) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (!data || !blockCount || blockCount > 0xFFFFu) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    return rw10(*this, m, true, lba, const_cast<uint8_t *>(data), blockCount, timeoutMs);
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
