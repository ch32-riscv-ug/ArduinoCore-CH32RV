/* USB CCID (smart card reader). TinyUSB has no CCID class, so the driver is
 * here (EspUsbDevice's, with static buffers instead of heap ones); a sketch
 * without a card reader links none of it. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

/* PC_to_RDR message types (CCID 1.1 section 6.1). */
#define PC_TO_RDR_SET_PARAMETERS   0x61
#define PC_TO_RDR_ICC_POWER_ON     0x62
#define PC_TO_RDR_ICC_POWER_OFF    0x63
#define PC_TO_RDR_GET_SLOT_STATUS  0x65
#define PC_TO_RDR_ICC_CLOCK        0x6A
#define PC_TO_RDR_T0_APDU          0x6B
#define PC_TO_RDR_GET_PARAMETERS   0x6C
#define PC_TO_RDR_RESET_PARAMETERS 0x6D
#define PC_TO_RDR_ESCAPE           0x6E
#define PC_TO_RDR_XFR_BLOCK        0x6F
#define PC_TO_RDR_ABORT            0x72
/* RDR_to_PC (section 6.2). */
#define RDR_TO_PC_DATA_BLOCK        0x80
#define RDR_TO_PC_SLOT_STATUS       0x81
#define RDR_TO_PC_PARAMETERS        0x82
#define RDR_TO_PC_ESCAPE            0x83
#define RDR_TO_PC_NOTIFY_SLOT_CHANGE 0x50
#define COMMAND_OK     0
#define COMMAND_FAILED 1
#define REQUEST_ABORT  0x01

#define HEADER_SIZE  10
#define MESSAGE_SIZE CH32RV_USB_DEVICE_CCID_BUFFER_SIZE
#define BULK_EP_SIZE 64
#define NOTIFY_EP_SIZE 8
#define NOTIFY_INTERVAL_MS 16

/* T=1 parameters, fixed: the slot is emulated, nothing to negotiate. */
static const uint8_t t1_parameters[7] = {0x11, 0x10, 0x00, 0x4D, 0x00, 0xFE, 0x00};

static uint32_t get32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/* The response type a failed message is answered with. */
static uint8_t response_type(uint8_t message_type)
{
    return message_type == PC_TO_RDR_XFR_BLOCK || message_type == PC_TO_RDR_ICC_POWER_ON ? RDR_TO_PC_DATA_BLOCK
                                                                                          : RDR_TO_PC_SLOT_STATUS;
}

size_t CH32RVUsbDeviceCcid::atr(uint8_t *buffer, size_t capacity) const
{
    if (!buffer || capacity < _atrLength) {
        return 0;
    }
    memcpy(buffer, _atr, _atrLength);
    return _atrLength;
}

uint8_t CH32RVUsbDeviceCcid::iccStatus() const
{
    if (!_cardPresent) {
        return CH32RV_USB_DEVICE_CCID_ICC_ABSENT;
    }
    return _cardPowered ? CH32RV_USB_DEVICE_CCID_ICC_ACTIVE : CH32RV_USB_DEVICE_CCID_ICC_INACTIVE;
}

#if CH32RV_TINYUSB_DEVICE

/* ---------------------------------------------------------- the driver */
static struct {
    uint8_t rhport;
    uint8_t ep_in, ep_out, ep_notify;
    uint16_t ep_in_size;
    /* One PC_to_RDR message being assembled: `received` counts all the host
     * sent for it, `stored` what fitted. */
    uint32_t received, stored;
    bool zlp_pending;
} drv;

CFG_TUSB_MEM_ALIGN static uint8_t ep_out_buf[BULK_EP_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t notify_buf[NOTIFY_EP_SIZE];
CFG_TUSB_MEM_ALIGN static uint8_t message[MESSAGE_SIZE];
/* Responses are built here and sent from here: CCID is one command at a
 * time, so the next one cannot overwrite a response still in flight. */
CFG_TUSB_MEM_ALIGN static uint8_t response[MESSAGE_SIZE];

static CH32RVUsbDeviceCcid *ccid;

static bool arm_receive(void)
{
    return drv.ep_out && usbd_edpt_xfer(drv.rhport, drv.ep_out, ep_out_buf, BULK_EP_SIZE, false);
}

static bool driver_send(size_t length)
{
    if (!drv.ep_in || !length || length > MESSAGE_SIZE || !usbd_edpt_claim(drv.rhport, drv.ep_in)) {
        return false;
    }
    drv.zlp_pending = drv.ep_in_size && length % drv.ep_in_size == 0;
    return usbd_edpt_xfer(drv.rhport, drv.ep_in, response, (uint16_t)length, false);
}

static bool driver_notify(const uint8_t *data, size_t length)
{
    if (!drv.ep_notify || !usbd_edpt_claim(drv.rhport, drv.ep_notify)) {
        return false;
    }
    memcpy(notify_buf, data, length);
    return usbd_edpt_xfer(drv.rhport, drv.ep_notify, notify_buf, (uint16_t)length, false);
}

static void ccidd_init(void)
{
    memset(&drv, 0, sizeof drv);
}

static bool ccidd_deinit(void)
{
    memset(&drv, 0, sizeof drv);
    return true;
}

static void ccidd_reset(uint8_t rhport)
{
    (void)rhport;
    memset(&drv, 0, sizeof drv);
    if (ccid) {
        ccid->handleBusDetached();
    }
}

static uint16_t ccidd_open(uint8_t rhport, tusb_desc_interface_t const *itf, uint16_t max_len)
{
    TU_VERIFY(itf->bInterfaceClass == 0x0B && itf->bInterfaceSubClass == 0 && itf->bInterfaceProtocol == 0, 0);
    const uint8_t *end = (const uint8_t *)itf + max_len;
    const uint8_t *p = tu_desc_next(itf);
    drv.rhport = rhport;
    drv.ep_in = drv.ep_out = drv.ep_notify = 0;
    drv.received = drv.stored = 0;
    while (tu_desc_in_bounds(p, end)) {
        const uint8_t type = tu_desc_type(p);
        if (type == TUSB_DESC_INTERFACE || type == TUSB_DESC_INTERFACE_ASSOCIATION) {
            break;
        }
        if (type == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)p;
            TU_ASSERT(usbd_edpt_open(rhport, ep), 0);
            if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                drv.ep_notify = ep->bEndpointAddress;
            } else if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                drv.ep_in = ep->bEndpointAddress;
                drv.ep_in_size = tu_edpt_packet_size(ep);
            } else {
                drv.ep_out = ep->bEndpointAddress;
            }
        }
        p = tu_desc_next(p);
    }
    TU_ASSERT(drv.ep_in && drv.ep_out, 0);
    TU_ASSERT(arm_receive(), 0);
    /* Configured: tell the host the slot state without its having to poll. */
    if (ccid) {
        ccid->handleBusAttached();
    }
    return (uint16_t)(p - (const uint8_t *)itf);
}

static bool ccidd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS) {
        return false;
    }
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }
    /* GET_CLOCK_FREQUENCIES / GET_DATA_RATES are only legal when the class
     * descriptor lists several; it lists one, so they stall. */
    if (request->bRequest == REQUEST_ABORT) {
        if (ccid) {
            ccid->handleAbort(tu_u16_low(request->wValue), tu_u16_high(request->wValue));
        }
        return tud_control_status(rhport, request);
    }
    return false;
}

static bool ccidd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred)
{
    (void)rhport;
    if (ep_addr == drv.ep_in) {
        if (drv.zlp_pending) {
            drv.zlp_pending = false;
            if (usbd_edpt_claim(drv.rhport, drv.ep_in)) {
                usbd_edpt_xfer(drv.rhport, drv.ep_in, response, 0, false);
            }
        }
        return true;
    }
    if (ep_addr == drv.ep_notify) {
        if (ccid) {
            ccid->handleNotifyComplete();
        }
        return true;
    }
    if (ep_addr != drv.ep_out) {
        return true;
    }
    if (result != XFER_RESULT_SUCCESS) {
        drv.received = drv.stored = 0;
        arm_receive();
        return true;
    }
    drv.received += xferred;
    if (drv.stored < MESSAGE_SIZE) {
        const uint32_t room = MESSAGE_SIZE - drv.stored;
        const uint32_t n = xferred < room ? xferred : room;
        memcpy(message + drv.stored, ep_out_buf, n);
        drv.stored += n;
    }
    if (drv.stored >= HEADER_SIZE) {
        const uint32_t total = HEADER_SIZE + get32(message + 1);
        if (drv.received >= total) {
            if (ccid && total <= MESSAGE_SIZE) {
                ccid->handleMessage(message, total);
            } else if (ccid) {
                ccid->handleOverrun(message[0], message[5], message[6]);
            }
            drv.received = drv.stored = 0;
        }
    } else if (xferred < BULK_EP_SIZE) {
        /* A short packet before a whole header: the host ended the transfer. */
        drv.received = drv.stored = 0;
    }
    arm_receive();
    return true;
}

static const usbd_class_driver_t ccid_driver = {
    NULL, ccidd_init, ccidd_deinit, ccidd_reset, ccidd_open, ccidd_control_xfer_cb, ccidd_xfer_cb, NULL, NULL,
};

/* One interface: bulk OUT/IN on endpoint n, interrupt IN on n + 1. */
static uint16_t ccid_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                                uint8_t *ep, uint8_t str)
{
    (void)self;
    const uint16_t len = 9 + 54 + 3 * 7;
    if (len > room) {
        return 0;
    }
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    const uint8_t head[] = {9, TUSB_DESC_INTERFACE, itf, 0, 3, 0x0B, 0, 0, str};
    memcpy(out, head, sizeof head);
    /* CCID class descriptor (CCID 1.1 section 5.1). */
    uint8_t *c = out + 9;
    memset(c, 0, 54);
    c[0] = 54;
    c[1] = 0x21;
    put16(c + 2, 0x0110);       /* bcdCCID 1.10 */
    c[5] = 0x07;                /* 5 V / 3 V / 1.8 V */
    put32(c + 6, 2);            /* T=1 */
    put32(c + 10, 3580);
    put32(c + 14, 3580);
    put32(c + 19, 9600);
    put32(c + 23, 9600);
    put32(c + 28, 254);         /* dwMaxIFSD */
    /* The reader does activation, clock, baud rate, PPS and parameters
     * itself, and exchanges whole short APDUs. */
    put32(c + 40, 0x000204FE);
    put32(c + 44, HEADER_SIZE + 261);
    c[48] = 0xFF;
    c[49] = 0xFF;
    c[53] = 1;
    const uint8_t eps[] = {
        7, TUSB_DESC_ENDPOINT, n, TUSB_XFER_BULK, BULK_EP_SIZE, 0, 0,
        7, TUSB_DESC_ENDPOINT, (uint8_t)(0x80 | n), TUSB_XFER_BULK, BULK_EP_SIZE, 0, 0,
        7, TUSB_DESC_ENDPOINT, (uint8_t)(0x80 | (n + 1)), TUSB_XFER_INTERRUPT, NOTIFY_EP_SIZE, 0, NOTIFY_INTERVAL_MS,
    };
    memcpy(out + 9 + 54, eps, sizeof eps);
    return len;
}

static const CH32RVUsbFunctionType ccid_type = {&ccid_driver, ccid_descriptor, 1, 1, nullptr, 0};

CH32RVUsbDeviceCcid::CH32RVUsbDeviceCcid(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, ccid_type, CH32RV_USB_CCID_NAME)
{
    if (attached()) {
        ccid = this;
    }
}

bool CH32RVUsbDeviceCcid::mounted() const
{
    return ccid == this && drv.ep_in && drv.ep_out;
}

/* ---------------------------------------------------------- the reader */
bool CH32RVUsbDeviceCcid::insertCard(const uint8_t *atr, size_t length)
{
    if (!atr || length < 2 || length > CH32RV_USB_DEVICE_CCID_MAX_ATR) {
        return false;
    }
    noInterrupts();
    memcpy(_atr, atr, length);
    _atrLength = (uint8_t)length;
    _cardPowered = false;
    _cardPresent = true;
    interrupts();
    notifySlotState();
    return true;
}

void CH32RVUsbDeviceCcid::removeCard()
{
    _cardPresent = false;
    _cardPowered = false;
    _atrLength = 0;
    notifySlotState();
}

/* RDR_to_PC_NotifySlotChange: bit 0 the state, bit 1 "changed". The
 * endpoint holds one notification until the host polls it; a change in the
 * meantime is sent once it frees up. */
void CH32RVUsbDeviceCcid::notifySlotState()
{
    const uint8_t m[2] = {RDR_TO_PC_NOTIFY_SLOT_CHANGE, (uint8_t)((_cardPresent ? 1 : 0) | 2)};
    _notifyPending = !driver_notify(m, sizeof m);
}

void CH32RVUsbDeviceCcid::handleNotifyComplete()
{
    if (_notifyPending) {
        _notifyPending = false;
        notifySlotState();
    }
}

void CH32RVUsbDeviceCcid::handleBusAttached()
{
    _cardPowered = false;
    _aborted = false;
    notifySlotState();
}

void CH32RVUsbDeviceCcid::handleBusDetached()
{
    _cardPowered = false;
    _aborted = false;
}

void CH32RVUsbDeviceCcid::handleAbort(uint8_t slot, uint8_t sequence)
{
    (void)sequence;
    /* Everything until the matching PC_to_RDR_Abort fails (section 5.3.1). */
    if (slot == 0) {
        _aborted = true;
    }
}

void CH32RVUsbDeviceCcid::writeHeader(uint8_t messageType, uint32_t dataLength, uint8_t slot, uint8_t sequence,
                                      uint8_t icc, uint8_t commandStatus, uint8_t error, uint8_t parameter)
{
    response[0] = messageType;
    put32(response + 1, dataLength);
    response[5] = slot;
    response[6] = sequence;
    response[7] = (uint8_t)((commandStatus << 6) | (icc & 3));
    response[8] = error;
    response[9] = parameter;
}

void CH32RVUsbDeviceCcid::sendStatus(uint8_t messageType, uint8_t slot, uint8_t sequence, uint8_t commandStatus,
                                     uint8_t error, uint8_t parameter)
{
    writeHeader(messageType, 0, slot, sequence, iccStatus(), commandStatus, error, parameter);
    driver_send(HEADER_SIZE);
}

void CH32RVUsbDeviceCcid::sendData(uint8_t messageType, uint8_t slot, uint8_t sequence, const uint8_t *data,
                                   size_t length, uint8_t parameter)
{
    if (length > MESSAGE_SIZE - HEADER_SIZE) {
        sendStatus(messageType, slot, sequence, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_XFR_OVERRUN, parameter);
        return;
    }
    writeHeader(messageType, (uint32_t)length, slot, sequence, iccStatus(), COMMAND_OK, 0, parameter);
    if (length && data != response + HEADER_SIZE) {
        memcpy(response + HEADER_SIZE, data, length);
    }
    driver_send(HEADER_SIZE + length);
}

void CH32RVUsbDeviceCcid::handleOverrun(uint8_t messageType, uint8_t slot, uint8_t sequence)
{
    _commandCount++;
    _lastMessageType = messageType;
    sendStatus(response_type(messageType), slot, sequence, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_XFR_OVERRUN,
               0);
}

void CH32RVUsbDeviceCcid::handleMessage(const uint8_t *m, size_t length)
{
    if (length < HEADER_SIZE) {
        return;
    }
    const uint8_t type = m[0];
    const uint32_t declared = get32(m + 1);
    const uint8_t slot = m[5], seq = m[6];
    const uint8_t *data = m + HEADER_SIZE;
    const size_t size = declared < length - HEADER_SIZE ? declared : length - HEADER_SIZE;
    _commandCount++;
    _lastMessageType = type;

    if (slot != 0) {
        writeHeader(response_type(type), 0, slot, seq, CH32RV_USB_DEVICE_CCID_ICC_ABSENT, COMMAND_FAILED,
                    CH32RV_USB_DEVICE_CCID_ERROR_BAD_SLOT, 0);
        driver_send(HEADER_SIZE);
        return;
    }
    if (_aborted && type != PC_TO_RDR_ABORT) {
        sendStatus(response_type(type), slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_CMD_ABORTED, 0);
        return;
    }
    uint8_t *const answer = response + HEADER_SIZE;
    const size_t capacity = MESSAGE_SIZE - HEADER_SIZE;
    switch (type) {
    case PC_TO_RDR_ICC_POWER_ON: {
        if (!_cardPresent) {
            sendStatus(RDR_TO_PC_DATA_BLOCK, slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
            return;
        }
        const bool was = _cardPowered;
        _cardPowered = true;
        if (!was && _onPower) {
            _onPower(true);
        }
        sendData(RDR_TO_PC_DATA_BLOCK, slot, seq, _atr, _atrLength, 0);
        return;
    }
    case PC_TO_RDR_ICC_POWER_OFF: {
        const bool was = _cardPowered;
        _cardPowered = false;
        if (was && _onPower) {
            _onPower(false);
        }
        sendStatus(RDR_TO_PC_SLOT_STATUS, slot, seq, COMMAND_OK, 0, 0);
        return;
    }
    case PC_TO_RDR_GET_SLOT_STATUS:
        sendStatus(RDR_TO_PC_SLOT_STATUS, slot, seq, COMMAND_OK, 0, 0);
        return;
    case PC_TO_RDR_XFR_BLOCK: {
        if (!_cardPresent || !_cardPowered) {
            sendStatus(RDR_TO_PC_DATA_BLOCK, slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
            return;
        }
        _apduCount++;
        size_t n;
        if (_onApdu) {
            n = _onApdu(data, size, answer, capacity);
        } else {
            /* No card application: "instruction not supported" (SW 6D00). */
            answer[0] = 0x6D;
            answer[1] = 0x00;
            n = 2;
        }
        if (!n) {
            sendStatus(RDR_TO_PC_DATA_BLOCK, slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
            return;
        }
        sendData(RDR_TO_PC_DATA_BLOCK, slot, seq, answer, n, 0);
        return;
    }
    case PC_TO_RDR_ESCAPE: {
        const size_t n = _onEscape ? _onEscape(data, size, answer, capacity) : 0;
        if (!n) {
            sendStatus(RDR_TO_PC_ESCAPE, slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED, 0);
            return;
        }
        sendData(RDR_TO_PC_ESCAPE, slot, seq, answer, n, 0);
        return;
    }
    case PC_TO_RDR_GET_PARAMETERS:
    case PC_TO_RDR_RESET_PARAMETERS:
        sendData(RDR_TO_PC_PARAMETERS, slot, seq, t1_parameters, sizeof t1_parameters, 1);
        return;
    case PC_TO_RDR_SET_PARAMETERS:
        if (m[7] != 1) {
            /* bError 7: the offending byte is bProtocolNum. */
            writeHeader(RDR_TO_PC_PARAMETERS, 0, slot, seq, iccStatus(), COMMAND_FAILED, 7, m[7]);
            driver_send(HEADER_SIZE);
            return;
        }
        sendData(RDR_TO_PC_PARAMETERS, slot, seq, t1_parameters, sizeof t1_parameters, 1);
        return;
    case PC_TO_RDR_ABORT:
        _aborted = false;
        sendStatus(RDR_TO_PC_SLOT_STATUS, slot, seq, COMMAND_OK, 0, 0);
        return;
    case PC_TO_RDR_ICC_CLOCK:
    case PC_TO_RDR_T0_APDU:
        sendStatus(RDR_TO_PC_SLOT_STATUS, slot, seq, COMMAND_OK, 0, 0);
        return;
    default:
        sendStatus(RDR_TO_PC_SLOT_STATUS, slot, seq, COMMAND_FAILED, CH32RV_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED, 0);
        return;
    }
}

#else

static uint16_t ccid_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint16_t, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType ccid_type = {nullptr, ccid_descriptor, 1, 1, nullptr, 0};

CH32RVUsbDeviceCcid::CH32RVUsbDeviceCcid(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, ccid_type, CH32RV_USB_CCID_NAME)
{
}

bool CH32RVUsbDeviceCcid::mounted() const { return false; }
bool CH32RVUsbDeviceCcid::insertCard(const uint8_t *, size_t) { return false; }
void CH32RVUsbDeviceCcid::removeCard() {}
void CH32RVUsbDeviceCcid::handleMessage(const uint8_t *, size_t) {}
void CH32RVUsbDeviceCcid::handleOverrun(uint8_t, uint8_t, uint8_t) {}
void CH32RVUsbDeviceCcid::handleAbort(uint8_t, uint8_t) {}
void CH32RVUsbDeviceCcid::handleNotifyComplete() {}
void CH32RVUsbDeviceCcid::handleBusAttached() {}
void CH32RVUsbDeviceCcid::handleBusDetached() {}

#endif
