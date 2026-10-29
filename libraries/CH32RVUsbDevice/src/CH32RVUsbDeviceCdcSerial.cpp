/* CDC-ACM serial on the device stack: line coding and state on EP0, data on a
 * bulk OUT / IN pair (CH32RVUsbDeviceBulkStream), an interrupt IN for
 * notifications (declared, never sent). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define CDC_PORTS_MAX 4

/* What usbDeliver() has to tell the sketch. */
#define PENDING_LINE_STATE  0x01
#define PENDING_LINE_CODING 0x02

/* Notification IN on endpoint n, data OUT and IN on n + 1. */
static bool cdc_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                               uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceCdcSerial &cdc = const_cast<CH32RVUsbDeviceCdcSerial &>(
        static_cast<const CH32RVUsbDeviceCdcSerial &>(self));
    if (!cdc.receiveReady()) {
        return cdc.refuseSettings();
    }
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    cdc._epNotify = (uint8_t)(0x80 | n);
    cdc._ep = (uint8_t)(n + 1);
    const uint16_t mps = ch32rv_usb_bulk_size(speed);
    const uint8_t d[] = {
        /* The association's iFunction names a composite child on Windows, so
         * two ports would show the same name without it. */
        8, 11, itf, 2, 2, 2, 0, str,
        9, 4, itf, 0, 1, 2, 2, 0, str,                              /* communication interface */
        5, 0x24, 0x00, 0x20, 0x01,                                  /* header, CDC 1.20 */
        5, 0x24, 0x01, 0x00, (uint8_t)(itf + 1),                    /* call management */
        4, 0x24, 0x02, 0x06,                                        /* ACM: line requests, break */
        5, 0x24, 0x06, itf, (uint8_t)(itf + 1),                     /* union */
        7, 5, (uint8_t)(0x80 | n), 3, 8, 0, 16,                     /* notification IN */
        9, 4, (uint8_t)(itf + 1), 0, 2, 0x0A, 0, 0, 0,              /* data interface */
        7, 5, (uint8_t)(n + 1), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
        7, 5, (uint8_t)(0x80 | (n + 1)), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
    };
    w.put(d, sizeof d);
    return true;
}

static const CH32RVUsbFunctionType cdc_type = {cdc_descriptor, 2, CDC_PORTS_MAX, nullptr, 0};

CH32RVUsbDeviceCdcSerial::CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceBulkStream(device, cdc_type, name ? name : CH32RV_USB_CDC_NAME, true)
{
}

void CH32RVUsbDeviceCdcSerial::begin(unsigned long baud)
{
    (void)baud;
    if (!device().started()) {      /* the core may have begun it already */
        device().begin();
    }
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceCdcSerial::usbReset()
{
    CH32RVUsbDeviceBulkStream::usbReset();
    if (_lineState.dtr || _lineState.rts) {
        handleLineState(false, false);
    }
}

bool CH32RVUsbDeviceCdcSerial::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    switch (s.bRequest) {
    case 0x20:                                   /* SET_LINE_CODING */
        if (stage == CH32RVUsbControlStage::Setup) {
            return device().controlOut(_coding, sizeof _coding);
        }
        if (stage == CH32RVUsbControlStage::Data) {
            handleLineCoding((uint32_t)_coding[0] | (uint32_t)_coding[1] << 8 | (uint32_t)_coding[2] << 16 |
                                 (uint32_t)_coding[3] << 24,
                             _coding[4], _coding[5], _coding[6]);
        }
        return true;
    case 0x21:                                   /* GET_LINE_CODING */
        return stage != CH32RVUsbControlStage::Setup || device().controlIn(_coding, sizeof _coding);
    case 0x22:                                   /* SET_CONTROL_LINE_STATE */
        if (stage == CH32RVUsbControlStage::Setup) {
            return device().controlStatus();
        }
        if (stage == CH32RVUsbControlStage::Ack) {
            handleLineState(s.wValue & 1, (s.wValue & 2) != 0);
        }
        return true;
    case 0x23:                                   /* SEND_BREAK */
        return stage != CH32RVUsbControlStage::Setup || device().controlStatus();
    }
    return false;
}

/* ---------------------------------------------------------- Stream */
int CH32RVUsbDeviceCdcSerial::read()
{
    uint8_t c;
    return read(&c, 1) ? c : -1;
}

bool CH32RVUsbDeviceCdcSerial::connected()
{
    ch32rv_usb_task();
    return device().ready() && _lineState.dtr;
}

int CH32RVUsbDeviceCdcSerial::availableForWrite()
{
    ch32rv_usb_task();
    return (int)writeAvailable();
}

bool CH32RVUsbDeviceCdcSerial::waitWritable(size_t bytes, uint32_t timeoutMs)
{
    if (bytes > writeCapacity()) {
        return false;
    }
    push();
    const uint32_t t0 = millis();
    while (connected()) {
        if (writeAvailable() >= bytes) {
            return true;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
        push();
    }
    return false;
}

void CH32RVUsbDeviceCdcSerial::flush()
{
    push();
    const uint32_t t0 = millis();
    while (!bulkIdle() && connected() && (uint32_t)(millis() - t0) < _txTimeoutMs) {
        push();
    }
}

size_t CH32RVUsbDeviceCdcSerial::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32RVUsbDeviceCdcSerial::write(const uint8_t *buf, size_t size)
{
    size_t done = 0;
    uint32_t t0 = millis();
    while (done < size && connected()) {
        const size_t before = done;
        done += bulkQueue(buf + done, size - done);
        if (done != before) {
            t0 = millis();
        } else if ((uint32_t)(millis() - t0) >= _txTimeoutMs) {
            break;                               /* the host keeps the port open but reads nothing */
        }
    }
    return done;
}

CH32RVUsbDeviceCdcSerial::operator bool()
{
    return connected();
}

bool CH32RVUsbDeviceCdcSerial::dtr()
{
    return _lineState.dtr;
}

bool CH32RVUsbDeviceCdcSerial::rts()
{
    return _lineState.rts;
}

uint32_t CH32RVUsbDeviceCdcSerial::baud()
{
    return _lineCoding.baud;
}

/* The 1200 bps touch: the host opens the port at 1200 baud and drops DTR.
 * Acting on the DTR drop (not on the baud change) lets the host finish its
 * own close before the device goes away. */
void CH32RVUsbDeviceCdcSerial::handleLineState(bool dtr, bool rts)
{
    _lineState.dtr = dtr;
    _lineState.rts = rts;
    if (!dtr && _bootTouch && _lineCoding.baud == 1200) {
        CH32RVUsbDevice::requestBootloader();
    }
    _pending |= PENDING_LINE_STATE;
}

void CH32RVUsbDeviceCdcSerial::handleLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity,
                                                uint8_t dataBits)
{
    _lineCoding.baud = baud;
    _lineCoding.stopBits = stopBits;
    _lineCoding.parity = parity;
    _lineCoding.dataBits = dataBits;
    _pending |= PENDING_LINE_CODING;
}

size_t CH32RVUsbDeviceCdcSerial::usbDeliver()
{
    const uint8_t pending = _pending;
    _pending = 0;
    size_t ran = 0;
    if ((pending & PENDING_LINE_STATE) && _onLineState) {
        _onLineState(_lineState);
        ran++;
    }
    if ((pending & PENDING_LINE_CODING) && _onLineCoding) {
        _onLineCoding(_lineCoding);
        ran++;
    }
    return ran + CH32RVUsbDeviceBulkStream::usbDeliver();
}
