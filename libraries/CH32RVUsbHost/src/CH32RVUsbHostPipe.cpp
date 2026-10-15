/* A bulk pipe pair on an interface no class driver took (vendor bulk, CCID).
 * See CH32RVUsbHostInternal.h. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_TINYUSB_HOST

static void in_done(tuh_xfer_t *xfer)
{
    CH32RVUsbHostPipe &p = *(CH32RVUsbHostPipe *)xfer->user_data;
    p.inBusy = false;
    if (!p.open || xfer->result != XFER_RESULT_SUCCESS) {
        return;
    }
    for (uint32_t i = 0; i < xfer->actual_len; i++) {
        const uint16_t next = (uint16_t)((p.head + 1u) % p.ringSize);
        if (next == p.tail) {
            break;                  /* full: the rest is lost (armed only with room) */
        }
        p.ring[p.head] = p.inBuf[i];
        p.head = next;
    }
}

static void arm_in(CH32RVUsbHostPipe &p)
{
    if (!p.open || !p.epIn || p.inBusy) {
        return;
    }
    const uint16_t used = (uint16_t)((p.head + p.ringSize - p.tail) % p.ringSize);
    if ((uint16_t)(p.ringSize - 1u - used) < p.mpsIn) {
        return;                     /* wait for the reader; the device is NAKed meanwhile */
    }
    tuh_xfer_t xfer = {};
    xfer.daddr = p.daddr;
    xfer.ep_addr = p.epIn;
    xfer.buflen = p.mpsIn;
    xfer.buffer = p.inBuf;
    xfer.complete_cb = in_done;
    xfer.user_data = (uintptr_t)&p;
    p.inBusy = true;
    if (!tuh_edpt_xfer(&xfer)) {
        p.inBusy = false;
    }
}

bool ch32rv_usb_host_pipe_open(CH32RVUsbHostPipe &p, uint8_t daddr, uint8_t itf, uint8_t cls, uint8_t *ring,
                               uint16_t ringSize)
{
    ch32rv_usb_host_pipe_close(p);
    const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
    const uint8_t *cfg = ch32rv_usb_host_scratch;
    bool in_itf = false;
    uint8_t found = 0xFF;
    tusb_desc_endpoint_t eps[3];
    uint8_t neps = 0;
    for (uint16_t at = 0; at + 2 <= len && cfg[at] >= 2; at = (uint16_t)(at + cfg[at])) {
        const uint8_t *d = cfg + at;
        if (d[1] == TUSB_DESC_INTERFACE && d[0] >= 9) {
            if (found != 0xFF) {
                break;              /* past the interface we took */
            }
            in_itf = d[3] == 0 && (itf == 0xFF ? d[5] == cls : d[2] == itf);
            if (in_itf) {
                found = d[2];
            }
        } else if (in_itf && d[1] == TUSB_DESC_ENDPOINT && d[0] >= 7 && neps < 3) {
            memcpy(&eps[neps++], d, sizeof(tusb_desc_endpoint_t));
        }
    }
    if (found == 0xFF) {
        return false;
    }
    memset(&p, 0, sizeof p);
    p.daddr = daddr;
    p.itf = found;
    p.ring = ring;
    p.ringSize = ringSize;
    for (uint8_t i = 0; i < neps; i++) {
        const tusb_desc_endpoint_t &e = eps[i];
        const uint8_t type = e.bmAttributes.xfer;
        const uint16_t mps = tu_edpt_packet_size(&e);
        if (type == TUSB_XFER_BULK && tu_edpt_dir(e.bEndpointAddress) == TUSB_DIR_IN && !p.epIn) {
            p.epIn = e.bEndpointAddress;
            p.mpsIn = mps;
        } else if (type == TUSB_XFER_BULK && tu_edpt_dir(e.bEndpointAddress) == TUSB_DIR_OUT && !p.epOut) {
            p.epOut = e.bEndpointAddress;
            p.mpsOut = mps;
        } else if (type == TUSB_XFER_INTERRUPT && tu_edpt_dir(e.bEndpointAddress) == TUSB_DIR_IN) {
            p.epInt = e.bEndpointAddress;
        } else {
            continue;
        }
        if (type != TUSB_XFER_INTERRUPT && !tuh_edpt_open(daddr, &e)) {
            return false;
        }
    }
    if (!p.epIn && !p.epOut) {
        return false;
    }
    if (p.mpsIn > sizeof p.inBuf) {
        p.mpsIn = sizeof p.inBuf;
    }
    p.open = true;
    ch32rv_usb_host_mark_claimed(daddr, found);
    arm_in(p);
    return true;
}

void ch32rv_usb_host_pipe_close(CH32RVUsbHostPipe &p)
{
    if (p.open && tuh_connected(p.daddr)) {
        if (p.epIn) {
            tuh_edpt_close(p.daddr, p.epIn);
        }
        if (p.epOut) {
            tuh_edpt_close(p.daddr, p.epOut);
        }
    }
    p.open = false;
    p.inBusy = false;
}

void ch32rv_usb_host_pipe_poll(CH32RVUsbHostPipe &p)
{
    if (p.open && !tuh_connected(p.daddr)) {
        p.open = false;
        p.inBusy = false;
        return;
    }
    arm_in(p);
}

uint16_t ch32rv_usb_host_pipe_available(const CH32RVUsbHostPipe &p)
{
    return (uint16_t)((p.head + p.ringSize - p.tail) % (p.ringSize ? p.ringSize : 1u));
}

uint16_t ch32rv_usb_host_pipe_read(CH32RVUsbHostPipe &p, uint8_t *out, uint16_t length)
{
    uint16_t n = 0;
    while (n < length && p.tail != p.head) {
        out[n++] = p.ring[p.tail];
        p.tail = (uint16_t)((p.tail + 1u) % p.ringSize);
    }
    arm_in(p);
    return n;
}

static void out_done(tuh_xfer_t *xfer)
{
    CH32RVUsbHostPipe &p = *(CH32RVUsbHostPipe *)xfer->user_data;
    p.outResult = xfer->result;
    p.outDone = true;
}

bool ch32rv_usb_host_pipe_write(CH32RVUsbHost &host, CH32RVUsbHostPipe &p, const uint8_t *data, size_t length,
                                uint32_t timeoutMs)
{
    if (!p.open || !p.epOut) {
        return host.fail(CH32RVUsbHostError::NotSupported);
    }
    if (length > 0xFFFFu || (length && !data)) {
        return host.fail(CH32RVUsbHostError::InvalidArgument);
    }
    p.outDone = false;
    p.outResult = XFER_RESULT_INVALID;
    tuh_xfer_t xfer = {};
    xfer.daddr = p.daddr;
    xfer.ep_addr = p.epOut;
    xfer.buflen = (uint32_t)length;
    xfer.buffer = const_cast<uint8_t *>(data);
    xfer.complete_cb = out_done;
    xfer.user_data = (uintptr_t)&p;
    if (!tuh_edpt_xfer(&xfer)) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    if (!ch32rv_usb_host_wait(p.outDone, timeoutMs)) {
        /* The caller's buffer must not be touched once we return. */
        tuh_edpt_abort_xfer(p.daddr, p.epOut);
        return host.fail(CH32RVUsbHostError::Timeout);
    }
    if (p.outResult != XFER_RESULT_SUCCESS) {
        return host.fail(ch32rv_usb_host_result_error(p.outResult));
    }
    return host.ok();
}

#endif
