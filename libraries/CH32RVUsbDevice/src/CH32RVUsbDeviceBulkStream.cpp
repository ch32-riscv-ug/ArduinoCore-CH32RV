/* The bulk pair CDC and Vendor share: FIFOs, per-transfer callbacks and the
 * borrowed transfers. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

CH32RVUsbDeviceBulkStream::CH32RVUsbDeviceBulkStream(CH32RVUsbDevice &device, const CH32RVUsbFunctionType &type,
                                                     const char *name, bool terminate)
    : CH32RVUsbDeviceFunction(device, type, name), _terminate(terminate), _terminateByDefault(terminate)
{
}

bool CH32RVUsbDeviceBulkStream::mounted() const
{
    return attached() && device().ready();
}

bool CH32RVUsbDeviceBulkStream::configure(const CH32RVUsbBulkStreamConfig &config)
{
    uint8_t *rx, *tx;
    uint16_t rxSize, txSize;
    if (device().started() || config.rxMode > CH32RVUsbBulkRxMode::Disabled ||
        config.streamTermination > CH32RVUsbStreamTermination::EndWithShortPacket ||
        !ch32rv_usb_bulk_buffer(config.rxBuffer, config.rxCapacity, _rxStore, sizeof _rxStore, rx, rxSize) ||
        !ch32rv_usb_bulk_buffer(config.txBuffer, config.txCapacity, _txStore, sizeof _txStore, tx, txSize)) {
        return false;
    }
    _config = config;
    _rx = rx;
    _rxSize = rxSize;
    _tx = tx;
    _txSize = txSize;
    _terminate = config.streamTermination == CH32RVUsbStreamTermination::FunctionDefault
                     ? _terminateByDefault
                     : config.streamTermination == CH32RVUsbStreamTermination::EndWithShortPacket;
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
    return true;
}

size_t CH32RVUsbDeviceBulkStream::writeAvailable() const
{
    if (!mounted() || _direct) {
        return 0;
    }
    const uint16_t used = _txHead >= _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(_txSize - _txTail + _txHead);
    return (size_t)(_txSize - 1 - used);
}

/* ---------------------------------------------------------- borrowed transfers */
const CH32RVUsbDeviceBulkStream::Extra CH32RVUsbDeviceBulkStream::extra = {extraTransferred, extraReset, extraDeliver,
                                                                         extraReceive};

void CH32RVUsbDeviceBulkStream::onRxData(RxDataCallback cb)
{
    _onRxData = cb;
    _extra = &extra;
    deliverWith(deliverThis);
}

CH32RVUsbTransferSubmission CH32RVUsbDeviceBulkStream::writeDirect(const void *buffer, size_t length,
                                                                   CH32RVUsbTransferTermination termination)
{
    CH32RVUsbTransferSubmission r;
    if (!buffer || !length || length > maxDirectTransfer()) {
        r.error = CH32RVUsbTransferError::BadArgument;
    } else if (!mounted()) {
        r.error = CH32RVUsbTransferError::NotMounted;
    } else if (_inBusy || _zlp || _directDone || _txHead != _txTail) {
        r.error = CH32RVUsbTransferError::Busy;     /* _directDone: the last one is not handed back yet */
    } else if (!device().transfer((uint8_t)(0x80 | _ep), const_cast<void *>(buffer),
                                  _directArmed = (uint16_t)(length < directPiece() ? length : directPiece()))) {
        r.error = CH32RVUsbTransferError::TransferFailed;
    } else {
        _extra = &extra;
        _inBusy = _direct = true;
        _txSending = 0;
        _directZlp = termination == CH32RVUsbTransferTermination::EndWithShortPacket && length % _packet == 0;
        if (++_directId == 0) {
            _directId = 1;
        }
        _directOut = CH32RVUsbTransferResult();
        _directOut.buffer = buffer;
        _directOut.requestedBytes = length;
        _directOut.id = r.id = _directId;
    }
    return r;
}

/* The loan in flight ends: report it once. From end() (Stopped) the sketch
 * gets it before end() returns - nothing delivers after - and from a cancel
 * before that returns. */
void CH32RVUsbDeviceBulkStream::directEnded(CH32RVUsbTransferStatus status, size_t actual, bool known)
{
    _direct = _directZlp = false;
    _directOut.status = status;
    _directOut.actualBytes = actual;
    _directOut.actualBytesKnown = known;
    _directDone = true;
    if (status == CH32RVUsbTransferStatus::Stopped || status == CH32RVUsbTransferStatus::Cancelled) {
        usbDeliver();
    }
}

CH32RVUsbTransferSubmission CH32RVUsbDeviceBulkStream::readDirect(void *buffer, size_t capacity)
{
    CH32RVUsbTransferSubmission r;
    if (_config.rxMode != CH32RVUsbBulkRxMode::Borrowed) {
        r.error = CH32RVUsbTransferError::NotSupported;
    } else if (!buffer || !capacity) {
        r.error = CH32RVUsbTransferError::BadArgument;
    } else if (!mounted()) {
        r.error = CH32RVUsbTransferError::NotMounted;
    } else if (capacity % _packet || capacity > maxDirectReceive()) {
        r.error = CH32RVUsbTransferError::BadArgument;
    } else if (_outBusy || _directInDone) {
        r.error = CH32RVUsbTransferError::Busy;
    } else if (!device().transfer(_ep, buffer,
                                  _directInArmed = (uint16_t)(capacity < directPiece() ? capacity : directPiece()))) {
        r.error = CH32RVUsbTransferError::TransferFailed;
    } else {
        _extra = &extra;
        _outBusy = _directRx = true;
        if (++_directInId == 0) {
            _directInId = 1;
        }
        _directIn = CH32RVUsbTransferResult();
        _directIn.buffer = buffer;
        _directIn.requestedBytes = capacity;
        _directIn.id = r.id = _directInId;
    }
    return r;
}

size_t CH32RVUsbDeviceBulkStream::maxDirectReceive() const
{
    return _config.rxMode == CH32RVUsbBulkRxMode::Borrowed ? DIRECT_MAX / _packet * _packet : 0;
}

void CH32RVUsbDeviceBulkStream::directRxEnded(CH32RVUsbTransferStatus status, size_t actual, bool known)
{
    _directRx = false;
    _directIn.status = status;
    _directIn.actualBytes = actual;
    _directIn.actualBytesKnown = known;
    _directInDone = true;
    if (status == CH32RVUsbTransferStatus::Stopped || status == CH32RVUsbTransferStatus::Cancelled) {
        usbDeliver();
    }
}

/* The controller drops the transfer and voids a completion not yet handled,
 * so the loan ends here. */
bool CH32RVUsbDeviceBulkStream::cancelDirectTx(uint32_t id)
{
    if (!_direct || (id && id != _directId)) {
        return false;
    }
    device().abort((uint8_t)(0x80 | _ep));
    _inBusy = false;
    directEnded(CH32RVUsbTransferStatus::Cancelled, _directOut.actualBytes, false);
    push();
    return true;
}

bool CH32RVUsbDeviceBulkStream::cancelDirectRx(uint32_t id)
{
    if (!_directRx || (id && id != _directInId)) {
        return false;
    }
    device().abort(_ep);
    _outBusy = false;
    directRxEnded(CH32RVUsbTransferStatus::Cancelled, _directIn.actualBytes, false);
    return true;
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceBulkStream::usbReset()
{
    if (_extra) {
        /* end() has cleared started() by now; a bus reset has not. */
        _extra->reset(*this, device().started() ? CH32RVUsbTransferStatus::BusReset
                                                : CH32RVUsbTransferStatus::Stopped);
    }
    _outBusy = _inBusy = _zlp = false;
    _rxHeld = 0;
}

void CH32RVUsbDeviceBulkStream::extraReset(CH32RVUsbDeviceBulkStream &s, CH32RVUsbTransferStatus why)
{
    if (s._direct) {
        s.directEnded(why, 0, false);
    }
    if (s._directRx) {
        s.directRxEnded(why, 0, false);
    }
}

void CH32RVUsbDeviceBulkStream::usbConfigured(CH32RVUsbSpeed speed)
{
    bulkConfigured(ch32rv_usb_bulk_size(speed));
}

void CH32RVUsbDeviceBulkStream::bulkConfigured(uint16_t packet)
{
    _packet = packet;
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
    receive();
}

uint16_t CH32RVUsbDeviceBulkStream::rxUsed() const
{
    return _rxEnd ? (uint16_t)(_rxEnd - _rxTail + _rxHead) : (uint16_t)(_rxHead - _rxTail);
}

/* The next OUT packet goes straight into the FIFO, once there is a whole
 * packet of contiguous room: the host waits on NAK until then, and nothing
 * is dropped. When the end of the buffer is too short, the data wraps early
 * and _rxEnd marks where. */
void CH32RVUsbDeviceBulkStream::receive()
{
    if (_outBusy || _rxHeld || !mounted() || _config.rxMode == CH32RVUsbBulkRxMode::Borrowed ||
        _config.rxMode == CH32RVUsbBulkRxMode::Disabled) {
        return;
    }
    if (_extra && _extra->receive(*this)) {
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
    _outBusy = device().transfer(_ep, _rx + _rxHead, _packet);
    _rxAt = _rx + _rxHead;
}

/* onRxData(): nothing is kept, each transfer takes the whole buffer, whole
 * packets of it, and ends early on a short packet. */
bool CH32RVUsbDeviceBulkStream::extraReceive(CH32RVUsbDeviceBulkStream &s)
{
    if (!s._onRxData) {
        return false;
    }
    s._rxHead = s._rxTail = s._rxEnd = 0;
    if (s._rxSize >= s._packet) {
        s._outBusy = s.device().transfer(s._ep, s._rx, (uint16_t)(s._rxSize / s._packet * s._packet));
        s._rxAt = s._rx;
    }
    return true;
}

/* The longest contiguous run of queued bytes goes out as one transfer; the
 * driver splits it into packets. */
void CH32RVUsbDeviceBulkStream::push()
{
    if (_inBusy || !mounted()) {
        return;
    }
    if (_txHead == _txTail) {
        if (_zlp && device().transfer((uint8_t)(0x80 | _ep), _tx, 0)) {
            _zlp = false;                        /* the last packet was full: end the transfer */
            _inBusy = true;
            _txSending = 0;
        }
        return;
    }
    _txSending = _txHead > _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(_txSize - _txTail);
    _inBusy = device().transfer((uint8_t)(0x80 | _ep), _tx + _txTail, _txSending);
}

/* A borrowed transfer's completion, or onRxData()'s; false for the FIFOs'. */
bool CH32RVUsbDeviceBulkStream::extraTransferred(CH32RVUsbDeviceBulkStream &s, uint8_t address, uint16_t length)
{
    if (address == s._ep) {
        if (s._directRx) {
            s._outBusy = false;
            s._directIn.actualBytes += length;
            if (length == s._directInArmed && s._directIn.actualBytes < s._directIn.requestedBytes) {
                /* A full piece: the transfer goes on into the next one. */
                const size_t left = s._directIn.requestedBytes - s._directIn.actualBytes;
                s._directInArmed = (uint16_t)(left < s.directPiece() ? left : s.directPiece());
                if (s.device().transfer(s._ep,
                                        (uint8_t *)const_cast<void *>(s._directIn.buffer) + s._directIn.actualBytes,
                                        s._directInArmed)) {
                    s._outBusy = true;
                    return true;
                }
                s.directRxEnded(CH32RVUsbTransferStatus::TransferError, s._directIn.actualBytes, true);
                return true;
            }
            s.directRxEnded(CH32RVUsbTransferStatus::Success, s._directIn.actualBytes, true);
            return true;
        }
        if (s._onRxData) {
            /* Held where it landed (where this transfer was armed: one armed
             * before onRxData() was set lands at the FIFO's head) until
             * usbDeliver() hands it over; the next transfer reuses the room. */
            s._outBusy = false;
            if (length) {
                s._rxHeld = length;
            } else {
                s.receive();
            }
            return true;
        }
        return false;
    }
    if (address != (uint8_t)(0x80 | s._ep) || !s._direct) {
        return false;
    }
    s._inBusy = false;
    s._directOut.actualBytes += length;
    if (s._directOut.actualBytes < s._directOut.requestedBytes) {
        const size_t left = s._directOut.requestedBytes - s._directOut.actualBytes;
        s._directArmed = (uint16_t)(left < s.directPiece() ? left : s.directPiece());
        if (s.device().transfer((uint8_t)(0x80 | s._ep),
                                (uint8_t *)const_cast<void *>(s._directOut.buffer) + s._directOut.actualBytes,
                                s._directArmed)) {
            s._inBusy = true;
            return true;
        }
        s.directEnded(CH32RVUsbTransferStatus::TransferError, s._directOut.actualBytes, true);
        s.push();
        return true;
    }
    if (s._directZlp && s.device().transfer((uint8_t)(0x80 | s._ep), s._tx, 0)) {
        s._directZlp = false;                    /* EndWithShortPacket */
        s._inBusy = true;
        return true;
    }
    s.directEnded(CH32RVUsbTransferStatus::Success, s._directOut.actualBytes, true);
    s.push();
    return true;
}

void CH32RVUsbDeviceBulkStream::usbTransferred(uint8_t address, uint16_t length)
{
    if (_extra && _extra->transferred(*this, address, length)) {
        return;
    }
    if (address == _ep) {
        _outBusy = false;
        _rxHead = (uint16_t)(_rxHead + length);
        receive();
        if (length) {
            handleRx();
        }
    } else if (address == (uint8_t)(0x80 | _ep)) {
        _inBusy = false;
        const uint16_t sent = _txSending;
        if (_txSending) {
            _txTail = (uint16_t)((_txTail + _txSending) % _txSize);
            _zlp = _terminate && _txHead == _txTail && (_txSending % _packet) == 0;
            _txSending = 0;
        }
        push();
        handleTxComplete(sent);
    }
}

void CH32RVUsbDeviceBulkStream::handleRx()
{
    _rxPending = true;
}

void CH32RVUsbDeviceBulkStream::handleTxComplete(size_t sentBytes)
{
    if (_onTxComplete) {
        _txDone += sentBytes;
    }
}

size_t CH32RVUsbDeviceBulkStream::extraDeliver(CH32RVUsbDeviceBulkStream &s)
{
    size_t ran = 0;
    if (s._rxHeld) {
        /* Still held during the call: nothing re-arms the endpoint over it. */
        if (s._onRxData) {
            s._onRxData(s._rxAt, s._rxHeld);
            ran++;
        }
        s._rxHeld = 0;
        s.receive();
    }
    if (s._directInDone) {
        /* Cleared first: the callback may post the next buffer. */
        const CH32RVUsbTransferResult result = s._directIn;
        s._directInDone = false;
        if (s._onDirectRx) {
            s._onDirectRx(result);
            ran++;
        }
    }
    if (s._directDone) {
        /* Cleared first: the callback may arm the next one. */
        const CH32RVUsbTransferResult result = s._directOut;
        s._directDone = false;
        if (s._onDirectTx) {
            s._onDirectTx(result);
            ran++;
        }
    }
    return ran;
}

size_t CH32RVUsbDeviceBulkStream::usbDeliver()
{
    size_t ran = _extra ? _extra->deliver(*this) : 0;
    if (_rxPending) {
        _rxPending = false;
        if (_onRx && rxUsed()) {
            _onRx(rxUsed());
            ran++;
        }
    }
    if (_txDone) {
        const size_t sent = _txDone;
        _txDone = 0;
        if (_onTxComplete) {
            _onTxComplete(sent);
            ran++;
        }
    }
    return ran;
}

/* ---------------------------------------------------------- the FIFOs */
int CH32RVUsbDeviceBulkStream::bulkAvailable()
{
    ch32rv_usb_task();
    return rxUsed();
}

int CH32RVUsbDeviceBulkStream::bulkPeek()
{
    ch32rv_usb_task();
    return rxUsed() ? _rx[_rxTail] : -1;
}

size_t CH32RVUsbDeviceBulkStream::bulkRead(uint8_t *buffer, size_t size)
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

size_t CH32RVUsbDeviceBulkStream::bulkQueue(const uint8_t *buffer, size_t size)
{
    if (!mounted() || !buffer) {
        return 0;
    }
    const size_t room = writeAvailable();
    const size_t n = size < room ? size : room;
    /* Up to the end of the buffer, then from its start. */
    const size_t first = n < (size_t)(_txSize - _txHead) ? n : (size_t)(_txSize - _txHead);
    memcpy(_tx + _txHead, buffer, first);
    memcpy(_tx, buffer + first, n - first);
    _txHead = (uint16_t)((_txHead + n) % _txSize);
    push();
    return n;
}
