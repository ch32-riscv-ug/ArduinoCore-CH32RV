/* CDC-ACM serial on the device stack: line coding and state on EP0, data on a
 * bulk OUT / IN pair, an interrupt IN for notifications (declared, never
 * sent). The ring buffers are the object's (or the sketch's, setBuffers()). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define CDC_PORTS_MAX 4

/* Notification IN on endpoint n, data OUT and IN on n + 1. */
static uint16_t cdc_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                               uint8_t *ep, uint8_t str, bool hs)
{
    CH32RVUsbDeviceCdcSerial &cdc = const_cast<CH32RVUsbDeviceCdcSerial &>(
        static_cast<const CH32RVUsbDeviceCdcSerial &>(self));
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    cdc._epNotify = (uint8_t)(0x80 | n);
    cdc._epData = (uint8_t)(n + 1);
    const uint16_t mps = ch32rv_usb_bulk_size(hs);
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
    if (sizeof d > room) {
        return 0;
    }
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType cdc_type = {nullptr, cdc_descriptor, 2, CDC_PORTS_MAX, nullptr, 0};

static uint16_t used(uint16_t head, uint16_t tail, uint16_t size)
{
    return head >= tail ? (uint16_t)(head - tail) : (uint16_t)(size - tail + head);
}

CH32RVUsbDeviceCdcSerial::CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceFunction(device, cdc_type, name ? name : CH32RV_USB_CDC_NAME)
{
}

void CH32RVUsbDeviceCdcSerial::begin(unsigned long baud)
{
    (void)baud;
    device().begin();
}

void CH32RVUsbDeviceCdcSerial::setBuffers(uint8_t *rx, uint16_t rxSize, uint8_t *tx, uint16_t txSize)
{
    if (device().started() || !rx || !tx || rxSize < 2 || txSize < 2) {
        return;
    }
    _rx = rx;
    _rxSize = rxSize;
    _tx = tx;
    _txSize = txSize;
    _rxHead = _rxTail = _txHead = _txTail = 0;
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceCdcSerial::usbReset()
{
    _outBusy = _inBusy = _zlp = false;
    if (_lineState.dtr || _lineState.rts) {
        handleLineState(false, false);
    }
}

void CH32RVUsbDeviceCdcSerial::usbConfigured(bool highSpeed)
{
    _packet = highSpeed ? 512 : 64;
    if (_packet > sizeof _outPacket) {
        _packet = sizeof _outPacket;             /* high speed: the packet buffers grow with that controller */
    }
    _rxHead = _rxTail = _txHead = _txTail = 0;
    receive();
}

/* Take the next OUT packet only while the ring can hold a whole one: the host
 * waits on NAK otherwise, and nothing is dropped. */
void CH32RVUsbDeviceCdcSerial::receive()
{
    if (_outBusy || !device().mounted() || (uint16_t)(_rxSize - 1 - used(_rxHead, _rxTail, _rxSize)) < _packet) {
        return;
    }
    _outBusy = device().transfer(_epData, _outPacket, _packet);
}

void CH32RVUsbDeviceCdcSerial::push()
{
    if (_inBusy || !device().mounted()) {
        return;
    }
    const uint16_t pending = used(_txHead, _txTail, _txSize);
    if (!pending && !_zlp) {
        return;
    }
    const uint16_t n = pending > _packet ? _packet : pending;
    for (uint16_t i = 0; i < n; i++) {
        _inPacket[i] = _tx[_txTail];
        _txTail = (uint16_t)(_txTail + 1 == _txSize ? 0 : _txTail + 1);
    }
    if (device().transfer((uint8_t)(0x80 | _epData), _inPacket, n)) {
        _inBusy = true;
        _zlp = n == _packet;                     /* a full packet that ends the data needs a ZLP */
    }
}

void CH32RVUsbDeviceCdcSerial::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _epData) {
        _outBusy = false;
        for (uint16_t i = 0; i < length; i++) {
            _rx[_rxHead] = _outPacket[i];
            _rxHead = (uint16_t)(_rxHead + 1 == _rxSize ? 0 : _rxHead + 1);
        }
        receive();
        if (length) {
            handleRx();
        }
    } else if (address == (uint8_t)(0x80 | _epData)) {
        _inBusy = false;
        push();
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
int CH32RVUsbDeviceCdcSerial::available()
{
    ch32rv_usb_task();
    return used(_rxHead, _rxTail, _rxSize);
}

int CH32RVUsbDeviceCdcSerial::peek()
{
    ch32rv_usb_task();
    return _rxHead == _rxTail ? -1 : _rx[_rxTail];
}

int CH32RVUsbDeviceCdcSerial::read()
{
    ch32rv_usb_task();
    if (_rxHead == _rxTail) {
        return -1;
    }
    const uint8_t c = _rx[_rxTail];
    _rxTail = (uint16_t)(_rxTail + 1 == _rxSize ? 0 : _rxTail + 1);
    receive();
    return c;
}

size_t CH32RVUsbDeviceCdcSerial::read(uint8_t *buffer, size_t size)
{
    ch32rv_usb_task();
    size_t n = 0;
    while (buffer && n < size && _rxHead != _rxTail) {
        buffer[n++] = _rx[_rxTail];
        _rxTail = (uint16_t)(_rxTail + 1 == _rxSize ? 0 : _rxTail + 1);
    }
    receive();
    return n;
}

bool CH32RVUsbDeviceCdcSerial::connected()
{
    ch32rv_usb_task();
    return device().mounted() && _lineState.dtr;
}

int CH32RVUsbDeviceCdcSerial::availableForWrite()
{
    ch32rv_usb_task();
    return (int)(_txSize - 1 - used(_txHead, _txTail, _txSize));
}

void CH32RVUsbDeviceCdcSerial::flush()
{
    push();
    const uint32_t t0 = millis();
    while ((_inBusy || _txHead != _txTail) && connected() && (uint32_t)(millis() - t0) < 1000u) {
        ch32rv_usb_task();
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
    while (done < size) {
        if (!connected()) {
            break;
        }
        while (done < size && (uint16_t)(_txSize - 1 - used(_txHead, _txTail, _txSize))) {
            _tx[_txHead] = buf[done++];
            _txHead = (uint16_t)(_txHead + 1 == _txSize ? 0 : _txHead + 1);
        }
        push();
    }
    push();
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
    if (_onLineState) {
        _onLineState(_lineState);
    }
}

void CH32RVUsbDeviceCdcSerial::handleLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity,
                                                uint8_t dataBits)
{
    _lineCoding.baud = baud;
    _lineCoding.stopBits = stopBits;
    _lineCoding.parity = parity;
    _lineCoding.dataBits = dataBits;
    if (_onLineCoding) {
        _onLineCoding(_lineCoding);
    }
}

void CH32RVUsbDeviceCdcSerial::handleRx()
{
    if (_onRx) {
        _onRx(used(_rxHead, _rxTail, _rxSize));
    }
}
