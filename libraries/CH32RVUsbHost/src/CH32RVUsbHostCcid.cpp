/* CH32RVUsbHost: CCID smart-card readers (USB CCID 1.1). Bulk messages over
 * the reader's interface, slot changes from its interrupt endpoint. One
 * reader open at a time. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

/* Message types (CCID 1.1 6.1, 6.2, 6.3). */
enum : uint8_t {
    PC_TO_RDR_ICC_POWER_ON = 0x62,
    PC_TO_RDR_ICC_POWER_OFF = 0x63,
    PC_TO_RDR_GET_SLOT_STATUS = 0x65,
    PC_TO_RDR_ESCAPE = 0x6B,
    PC_TO_RDR_XFR_BLOCK = 0x6F,
    RDR_TO_PC_DATA_BLOCK = 0x80,
    RDR_TO_PC_SLOT_STATUS = 0x81,
    RDR_TO_PC_ESCAPE = 0x83,
    RDR_TO_PC_NOTIFY_SLOT_CHANGE = 0x50,
};

CH32RVUsbHostPipe pipe;
uint8_t own_ring[CH32RV_USB_HOST_CCID_BUFFER_SIZE];
uint8_t *ring = own_ring;
CH32RVUsbHostCcidInterface info;
uint8_t seq;
uint8_t atr[33];
uint8_t atr_len;
uint8_t own_message[CH32RV_USB_HOST_CCID_BUFFER_SIZE];
uint8_t *message = own_message;   /* one response, assembled */
uint16_t buffer_size = CH32RV_USB_HOST_CCID_BUFFER_SIZE;
uint8_t slot_bits;                                    /* last NotifySlotChange */
bool int_busy;
uint8_t int_buf[8] __attribute__((aligned(4)));

void int_done(void *user, uint8_t result, uint16_t length);

void arm_int()
{
    if (!pipe.open || !pipe.epInt || int_busy) {
        return;
    }
    int_busy = ch32rv_usb_host_xfer(pipe.daddr, pipe.epInt, int_buf, sizeof int_buf, int_done, nullptr);
}

volatile bool slot_event;
uint8_t slot_event_bits;

void int_done(void *user, uint8_t result, uint16_t length)
{
    (void)user;
    int_busy = false;
    if (result == CH32RV_USBH_OK && length >= 2 &&
        int_buf[0] == RDR_TO_PC_NOTIFY_SLOT_CHANGE) {
        slot_event_bits = int_buf[1];
        slot_event = true;
    }
}

void poll(CH32RVUsbHost &host)
{
    ch32rv_usb_host_pipe_poll(pipe);
    if (!pipe.open) {
        return;
    }
    arm_int();
    if (slot_event) {
        slot_event = false;
        /* Slot 0: bit 0 present, bit 1 changed. */
        const bool present = slot_event_bits & 0x01;
        const bool changed = slot_event_bits & 0x02;
        if (changed || present != (bool)(slot_bits & 1)) {
            CH32RVUsbHostCcidSlotEvent e;
            e.address = pipe.daddr;
            e.slot = 0;
            e.present = present;
            if (present && host._ccidInserted) {
                host._ccidInserted(e);
            } else if (!present && host._ccidRemoved) {
                host._ccidRemoved(e);
            }
        }
        slot_bits = slot_event_bits;
    }
}

void detach(uint8_t daddr)
{
    if (pipe.open && pipe.daddr == daddr) {
        pipe.open = false;
        pipe.inBusy = false;
        int_busy = false;
    }
}

CH32RVUsbHostGroup ccid_group = {nullptr, nullptr, poll, detach, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(ccid_group); }
} join;

uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read the reader's class descriptor (0x21 inside the interface). */
void read_class_descriptor(uint8_t daddr, uint8_t itf)
{
    const uint16_t len = ch32rv_usb_host_config_descriptor(daddr);
    const uint8_t *cfg = ch32rv_usb_host_scratch;
    bool here = false;
    for (uint16_t at = 0; at + 2 <= len && cfg[at] >= 2; at = (uint16_t)(at + cfg[at])) {
        const uint8_t *d = cfg + at;
        if (d[1] == 4) {
            here = d[2] == itf;
        } else if (here && d[1] == 0x21 && d[0] >= 0x36) {
            info.hasClassDescriptor = true;
            info.bcdCCID = (uint16_t)(d[2] | (d[3] << 8));
            info.slotCount = (uint8_t)(d[4] + 1u);
            info.protocols = le32(d + 6);
            info.features = le32(d + 40);
            info.maxMessageLength = le32(d + 44);
            return;
        }
    }
}

bool matches(uint8_t address)
{
    return pipe.open && (address == CH32RV_USB_HOST_ANY_ADDRESS || pipe.daddr == address);
}

/* Send one command and wait for its response (time extensions waited out).
 * The response lands in message[]; its data length goes to *dataLength. */
bool exchange(CH32RVUsbHost &host, uint8_t type, uint8_t slot, const uint8_t params[3], const uint8_t *data,
              size_t length, uint32_t *dataLength, uint32_t timeoutMs)
{
    if (10u + length > buffer_size) {
        return host.fail(CH32RVUsbHostError::InvalidArgument);
    }
    uint8_t *cmd = message;
    cmd[0] = type;
    cmd[1] = (uint8_t)length;
    cmd[2] = (uint8_t)(length >> 8);
    cmd[3] = (uint8_t)(length >> 16);
    cmd[4] = (uint8_t)(length >> 24);
    cmd[5] = slot;
    cmd[6] = ++seq;
    cmd[7] = params[0];
    cmd[8] = params[1];
    cmd[9] = params[2];
    if (length) {
        memcpy(cmd + 10, data, length);
    }
    /* Anything left over from an earlier exchange is stale. */
    uint8_t junk[16];
    while (ch32rv_usb_host_pipe_read(pipe, junk, sizeof junk)) {
    }
    if (!ch32rv_usb_host_pipe_write(host, pipe, cmd, 10u + length, timeoutMs)) {
        return false;
    }
    const uint32_t t0 = millis();
    uint32_t have = 0;
    for (;;) {
        ch32rv_usb_host_pump();
        ch32rv_usb_host_pipe_poll(pipe);
        have += ch32rv_usb_host_pipe_read(pipe, message + have, (uint16_t)(buffer_size - have));
        if (have >= 10) {
            const uint32_t want = 10u + le32(message + 1);
            if (want > buffer_size) {
                return host.fail(CH32RVUsbHostError::NoMemory);
            }
            if (have >= want) {
                if (message[6] != seq ||
                    (message[7] >> 6) == CH32RV_USB_HOST_CCID_COMMAND_TIME_EXTENSION) {
                    /* someone else's, or the reader asking for more time: drop
                     * this message, keep whatever followed it */
                    memmove(message, message + want, have - want);
                    have -= want;
                    continue;
                }
                *dataLength = want - 10u;
                return host.ok();
            }
        }
        if (!pipe.open) {
            return host.fail(CH32RVUsbHostError::NotConnected);
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return host.fail(CH32RVUsbHostError::Timeout);
        }
    }
}

}  // namespace

bool CH32RVUsbHost::setCcidBuffers(uint8_t *ringBuffer, uint8_t *messageBuffer, size_t size)
{
    if (!ringBuffer || !messageBuffer || size < 74 || size > 0xFFFF || pipe.open) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    ring = ringBuffer;
    message = messageBuffer;
    buffer_size = (uint16_t)size;
    return ok();
}

bool CH32RVUsbHost::ccidOpen(uint8_t address, uint8_t interfaceNumber)
{
    if (!_started) {
        return fail(CH32RVUsbHostError::NotStarted);
    }
    for (uint8_t a = 1; a <= CH32RV_USB_HOST_DEVICES; a++) {
        if ((address != CH32RV_USB_HOST_ANY_ADDRESS && a != address) || !ch32rv_usb_host_device(a)) {
            continue;
        }
        if (!ch32rv_usb_host_pipe_open(pipe, a, interfaceNumber, 0x0B, ring, buffer_size)) {
            continue;
        }
        info = CH32RVUsbHostCcidInterface();
        info.address = a;
        info.interfaceNumber = pipe.itf;
        info.inEndpoint = pipe.epIn;
        info.outEndpoint = pipe.epOut;
        info.interruptEndpoint = pipe.epInt;
        info.inMaxPacketSize = pipe.mpsIn;
        info.outMaxPacketSize = pipe.mpsOut;
        read_class_descriptor(a, pipe.itf);
        if (pipe.epInt) {
            /* The interrupt endpoint was skipped by the pipe: open it here. */
            const uint16_t len = ch32rv_usb_host_config_descriptor(a);
            const uint8_t *cfg = ch32rv_usb_host_scratch;
            for (uint16_t at = 0; at + 7 <= len && cfg[at] >= 2; at = (uint16_t)(at + cfg[at])) {
                if (cfg[at + 1] == 5 && cfg[at + 2] == pipe.epInt) {
                    if (!ch32rv_usb_host_ep_open(a, cfg + at)) {
                        pipe.epInt = 0;
                    }
                    break;
                }
            }
        }
        seq = 0;
        atr_len = 0;
        slot_bits = 0;
        int_busy = false;
        arm_int();
        return ok();
    }
    return fail(CH32RVUsbHostError::NotSupported);
}

void CH32RVUsbHost::ccidClose()
{
    if (pipe.open && pipe.epInt && ch32rv_usb_host_connected(pipe.daddr)) {
        ch32rv_usb_host_ep_close(pipe.daddr, pipe.epInt);
    }
    ch32rv_usb_host_pipe_close(pipe);
    int_busy = false;
}

bool CH32RVUsbHost::ccidReady(uint8_t address) const
{
    return matches(address);
}

bool CH32RVUsbHost::ccidGetInterface(CH32RVUsbHostCcidInterface &out) const
{
    if (!pipe.open) {
        return false;
    }
    out = info;
    return true;
}

bool CH32RVUsbHost::ccidGetStatus(CH32RVUsbHostCcidStatus &status, uint8_t slot, uint8_t address,
                                  uint32_t timeoutMs)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    const uint8_t params[3] = {0, 0, 0};
    uint32_t n = 0;
    if (!exchange(*this, PC_TO_RDR_GET_SLOT_STATUS, slot, params, nullptr, 0, &n, timeoutMs)) {
        return false;
    }
    status = CH32RVUsbHostCcidStatus();
    status.address = pipe.daddr;
    status.slot = slot;
    status.iccStatus = (CH32RVUsbHostCcidIccStatus)(message[7] & 3);
    status.commandStatus = (CH32RVUsbHostCcidCommandStatus)(message[7] >> 6);
    status.error = message[8];
    _ccidError = message[8];
    status.present = status.iccStatus != CH32RV_USB_HOST_CCID_ICC_ABSENT;
    status.active = status.iccStatus == CH32RV_USB_HOST_CCID_ICC_ACTIVE;
    return true;
}

bool CH32RVUsbHost::ccidCardPresent(uint8_t slot, uint8_t address)
{
    CH32RVUsbHostCcidStatus s;
    return ccidGetStatus(s, slot, address) && s.present;
}

bool CH32RVUsbHost::ccidPowerOn(uint8_t *atrOut, size_t atrCapacity, size_t *atrLength,
                                CH32RVUsbHostCcidVoltage voltage, uint8_t slot, uint8_t address,
                                uint32_t timeoutMs)
{
    if (atrLength) {
        *atrLength = 0;
    }
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    const uint8_t params[3] = {(uint8_t)voltage, 0, 0};
    uint32_t n = 0;
    if (!exchange(*this, PC_TO_RDR_ICC_POWER_ON, slot, params, nullptr, 0, &n, timeoutMs)) {
        return false;
    }
    _ccidError = message[8];
    if (message[0] != RDR_TO_PC_DATA_BLOCK) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    if ((message[7] >> 6) != CH32RV_USB_HOST_CCID_COMMAND_OK) {
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    atr_len = (uint8_t)ch32rv_min(n, (uint32_t)sizeof atr);
    memcpy(atr, message + 10, atr_len);
    if (atrOut && atrCapacity) {
        const size_t k = ch32rv_min((size_t)atr_len, atrCapacity);
        memcpy(atrOut, atr, k);
        if (atrLength) {
            *atrLength = k;
        }
    }
    return true;
}

bool CH32RVUsbHost::ccidPowerOff(uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    const uint8_t params[3] = {0, 0, 0};
    uint32_t n = 0;
    if (!exchange(*this, PC_TO_RDR_ICC_POWER_OFF, slot, params, nullptr, 0, &n, timeoutMs)) {
        return false;
    }
    _ccidError = message[8];
    atr_len = 0;
    return message[0] == RDR_TO_PC_SLOT_STATUS ? true : fail(CH32RVUsbHostError::InvalidResponse);
}

size_t CH32RVUsbHost::ccidGetAtr(uint8_t *buffer, size_t capacity) const
{
    const size_t k = ch32rv_min((size_t)atr_len, capacity);
    if (buffer && k) {
        memcpy(buffer, atr, k);
    }
    return k;
}

static bool data_exchange(CH32RVUsbHost &host, uint8_t type, uint8_t expect, const uint8_t *tx, size_t txLength,
                          uint8_t *rx, size_t rxCapacity, size_t *rxLength, uint8_t slot, uint32_t timeoutMs)
{
    if (rxLength) {
        *rxLength = 0;
    }
    const uint8_t params[3] = {0, 0, 0};
    uint32_t n = 0;
    if (!exchange(host, type, slot, params, tx, txLength, &n, timeoutMs)) {
        return false;
    }
    host._ccidError = message[8];
    if (message[0] != expect) {
        return host.fail(CH32RVUsbHostError::InvalidResponse);
    }
    if ((message[7] >> 6) != CH32RV_USB_HOST_CCID_COMMAND_OK) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    if (n > rxCapacity) {
        return host.fail(CH32RVUsbHostError::NoMemory);
    }
    if (rx && n) {
        memcpy(rx, message + 10, n);
    }
    if (rxLength) {
        *rxLength = n;
    }
    return host.ok();
}

bool CH32RVUsbHost::ccidTransfer(const uint8_t *tx, size_t txLength, uint8_t *rx, size_t rxCapacity,
                                 size_t *rxLength, uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return data_exchange(*this, PC_TO_RDR_XFR_BLOCK, RDR_TO_PC_DATA_BLOCK, tx, txLength, rx, rxCapacity, rxLength,
                         slot, timeoutMs);
}

bool CH32RVUsbHost::ccidApdu(const uint8_t *apdu, size_t apduLength, uint8_t *response, size_t responseCapacity,
                             size_t *responseLength, uint16_t *statusWord, uint8_t slot, uint8_t address,
                             uint32_t timeoutMs)
{
    size_t n = 0;
    if (!ccidTransfer(apdu, apduLength, response, responseCapacity, &n, slot, address, timeoutMs)) {
        if (responseLength) {
            *responseLength = 0;
        }
        return false;
    }
    if (n < 2) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    /* As EspUsbHost: the response without SW1 SW2, which go to *statusWord. */
    if (statusWord) {
        *statusWord = (uint16_t)((response[n - 2] << 8) | response[n - 1]);
    }
    if (responseLength) {
        *responseLength = n - 2u;
    }
    return ok();
}

bool CH32RVUsbHost::ccidEscape(const uint8_t *tx, size_t txLength, uint8_t *rx, size_t rxCapacity,
                               size_t *rxLength, uint8_t slot, uint8_t address, uint32_t timeoutMs)
{
    if (!matches(address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return data_exchange(*this, PC_TO_RDR_ESCAPE, RDR_TO_PC_ESCAPE, tx, txLength, rx, rxCapacity, rxLength, slot,
                         timeoutMs);
}

#else /* no host */

bool CH32RVUsbHost::setCcidBuffers(uint8_t *, uint8_t *, size_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::ccidOpen(uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::ccidClose() {}
bool CH32RVUsbHost::ccidReady(uint8_t) const { return false; }
bool CH32RVUsbHost::ccidGetInterface(CH32RVUsbHostCcidInterface &) const { return false; }
bool CH32RVUsbHost::ccidGetStatus(CH32RVUsbHostCcidStatus &, uint8_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::ccidCardPresent(uint8_t, uint8_t) { return false; }
bool CH32RVUsbHost::ccidPowerOn(uint8_t *, size_t, size_t *, CH32RVUsbHostCcidVoltage, uint8_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::ccidPowerOff(uint8_t, uint8_t, uint32_t) { return fail(CH32RVUsbHostError::Unsupported); }
size_t CH32RVUsbHost::ccidGetAtr(uint8_t *, size_t) const { return 0; }
bool CH32RVUsbHost::ccidTransfer(const uint8_t *, size_t, uint8_t *, size_t, size_t *, uint8_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::ccidApdu(const uint8_t *, size_t, uint8_t *, size_t, size_t *, uint16_t *, uint8_t, uint8_t,
                             uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::ccidEscape(const uint8_t *, size_t, uint8_t *, size_t, size_t *, uint8_t, uint8_t, uint32_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}

#endif
