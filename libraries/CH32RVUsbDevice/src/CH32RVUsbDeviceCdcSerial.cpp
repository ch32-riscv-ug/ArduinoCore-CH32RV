/* CDC-ACM serial on the device stack: line coding and state on EP0, data on a
 * bulk OUT / IN pair, an interrupt IN for notifications (declared, never
 * sent). The ring buffers are the object's (or the sketch's, setBuffers()). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#define CDC_PORTS_MAX 4

/* What usbDeliver() has to tell the sketch. */
#define PENDING_LINE_STATE  0x01
#define PENDING_LINE_CODING 0x02
#define PENDING_RX          0x04

/* Notification IN on endpoint n, data OUT and IN on n + 1. */
static bool cdc_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                               uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceCdcSerial &cdc = const_cast<CH32RVUsbDeviceCdcSerial &>(
        static_cast<const CH32RVUsbDeviceCdcSerial &>(self));
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    cdc._epNotify = (uint8_t)(0x80 | n);
    cdc._epData = (uint8_t)(n + 1);
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
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceCdcSerial::usbReset()
{
    _outBusy = _inBusy = _zlp = false;
    if (_lineState.dtr || _lineState.rts) {
        handleLineState(false, false);
    }
}

void CH32RVUsbDeviceCdcSerial::usbConfigured(CH32RVUsbSpeed speed)
{
    _packet = ch32rv_usb_bulk_size(speed);
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
    receive();
}

uint16_t CH32RVUsbDeviceCdcSerial::rxUsed() const
{
    return _rxEnd ? (uint16_t)(_rxEnd - _rxTail + _rxHead) : (uint16_t)(_rxHead - _rxTail);
}

/* The next OUT packet goes straight into the buffer, once there is a whole
 * packet of contiguous room: the host waits on NAK until then, and nothing is
 * dropped. When the end of the buffer is too short, the data wraps early and
 * _rxEnd marks where. */
void CH32RVUsbDeviceCdcSerial::receive()
{
    if (_outBusy || !device().mounted()) {
        return;
    }
    if (!_rxEnd && _rxHead == _rxTail) {
        _rxHead = _rxTail = 0;                   /* empty: all the room in one piece */
    }
    if (!_rxEnd && (uint16_t)(_rxSize - _rxHead) < _packet) {
        if (_rxTail <= _packet) {
            return;                              /* the front is still being read */
        }
        _rxEnd = _rxHead;
        _rxHead = 0;
    }
    if (_rxEnd && (uint16_t)(_rxTail - _rxHead) <= _packet) {
        return;
    }
    _outBusy = device().transfer(_epData, _rx + _rxHead, _packet);
}

/* The longest contiguous run of queued bytes goes out as one transfer; the
 * driver splits it into packets. */
void CH32RVUsbDeviceCdcSerial::push()
{
    if (_inBusy || !device().mounted()) {
        return;
    }
    if (_txHead == _txTail) {
        if (_zlp && device().transfer((uint8_t)(0x80 | _epData), _tx, 0)) {
            _zlp = false;                        /* the last packet was full: end the transfer */
            _inBusy = true;
            _txSending = 0;
        }
        return;
    }
    _txSending = _txHead > _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(_txSize - _txTail);
    _inBusy = device().transfer((uint8_t)(0x80 | _epData), _tx + _txTail, _txSending);
}

void CH32RVUsbDeviceCdcSerial::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _epData) {
        _outBusy = false;
        _rxHead = (uint16_t)(_rxHead + length);
        receive();
        if (length) {
            handleRx();
        }
    } else if (address == (uint8_t)(0x80 | _epData)) {
        _inBusy = false;
        if (_txSending) {
            _txTail = (uint16_t)((_txTail + _txSending) % _txSize);
            _zlp = _txHead == _txTail && (_txSending % _packet) == 0;
            _txSending = 0;
        }
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
    return rxUsed();
}

int CH32RVUsbDeviceCdcSerial::peek()
{
    ch32rv_usb_task();
    return rxUsed() ? _rx[_rxTail] : -1;
}

int CH32RVUsbDeviceCdcSerial::read()
{
    uint8_t c;
    return read(&c, 1) ? c : -1;
}

size_t CH32RVUsbDeviceCdcSerial::read(uint8_t *buffer, size_t size)
{
    ch32rv_usb_task();
    size_t n = 0;
    while (buffer && n < size && rxUsed()) {
        buffer[n++] = _rx[_rxTail++];
        if (_rxEnd && _rxTail == _rxEnd) {
            _rxTail = 0;
            _rxEnd = 0;
        }
    }
    receive();
    return n;
}

bool CH32RVUsbDeviceCdcSerial::connected()
{
    ch32rv_usb_task();
    return device().mounted() && _lineState.dtr;
}

static uint16_t tx_free(uint16_t head, uint16_t tail, uint16_t size)
{
    return (uint16_t)(size - 1 - (head >= tail ? head - tail : size - tail + head));
}

int CH32RVUsbDeviceCdcSerial::availableForWrite()
{
    ch32rv_usb_task();
    return tx_free(_txHead, _txTail, _txSize);
}

void CH32RVUsbDeviceCdcSerial::flush()
{
    push();
    const uint32_t t0 = millis();
    while ((_inBusy || _txHead != _txTail) && connected() && (uint32_t)(millis() - t0) < 1000u) {
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
    while (done < size && connected()) {
        while (done < size && tx_free(_txHead, _txTail, _txSize)) {
            _tx[_txHead] = buf[done++];
            _txHead = (uint16_t)(_txHead + 1 == _txSize ? 0 : _txHead + 1);
        }
        push();
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

void CH32RVUsbDeviceCdcSerial::handleRx()
{
    _pending |= PENDING_RX;
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
    if ((pending & PENDING_RX) && _onRx && rxUsed()) {
        _onRx(rxUsed());
        ran++;
    }
    return ran;
}
