/* Vendor (bulk) on the device stack, buffered. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>


CH32RVUsbTransferSubmission CH32RVUsbDeviceVendor::writeDirect(const void *buffer, size_t length,
                                                             CH32RVUsbTransferTermination termination)
{
    CH32RVUsbTransferSubmission r;
    if (!buffer || !length || length > maxDirectTransfer()) {
        r.error = CH32RVUsbTransferError::BadArgument;
    } else if (!mounted()) {
        r.error = CH32RVUsbTransferError::NotMounted;
    } else if (_inBusy || _zlp || _directDone || _txHead != _txTail) {
        r.error = CH32RVUsbTransferError::Busy;     /* _directDone: the last one is not handed back yet */
    } else if (!device().transfer((uint8_t)(0x80 | _ep), const_cast<void *>(buffer), (uint16_t)length)) {
        r.error = CH32RVUsbTransferError::TransferFailed;
    } else {
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
 * gets it before end() returns - nothing delivers after. */
void CH32RVUsbDeviceVendor::directEnded(CH32RVUsbTransferStatus status, size_t actual, bool known)
{
    _direct = _directZlp = false;
    _directOut.status = status;
    _directOut.actualBytes = actual;
    _directOut.actualBytesKnown = known;
    _directDone = true;
    if (status == CH32RVUsbTransferStatus::Stopped) {
        usbDeliver();
    }
}

static uint16_t clamp_ep(uint16_t n)
{
    return n >= 64 ? 64 : n >= 32 ? 32 : n >= 16 ? 16 : 8;
}

#define VENDORS_MAX 2

static CH32RVUsbDeviceVendor *vendors[VENDORS_MAX];

static bool vendor_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                                  uint8_t str, CH32RVUsbSpeed speed)
{
    CH32RVUsbDeviceVendor &v = const_cast<CH32RVUsbDeviceVendor &>(static_cast<const CH32RVUsbDeviceVendor &>(self));
    const uint8_t n = (*ep)++;
    v._ep = n;
    const uint16_t mps = speed == CH32RVUsbSpeed::Full ? v.endpointSize() : ch32rv_usb_bulk_size(speed);
    const uint8_t d[] = {
        9, 4, itf, 0, 2, 0xFF, v.interfaceSubclass(), v.interfaceProtocol(), str,
        7, 5, n, 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
        7, 5, (uint8_t)(0x80 | n), 2, (uint8_t)mps, (uint8_t)(mps >> 8), 0,
    };
    w.put(d, sizeof d);
    return true;
}

/* winusb 2: the compatible ID and DeviceInterfaceGUIDs. */
static const CH32RVUsbFunctionType vendor_type = {vendor_descriptor, 1, VENDORS_MAX, nullptr, 2};

/* Vendor requests to the device that the platform descriptors do not answer. */
static bool vendor_request(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    for (uint8_t i = 0; i < VENDORS_MAX; i++) {
        if (vendors[i] && vendors[i]->handleControlRequest(stage, setup)) {
            return true;
        }
    }
    return false;
}

CH32RVUsbDeviceVendor::CH32RVUsbDeviceVendor(CH32RVUsbDevice &device, uint16_t endpointSize)
    : CH32RVUsbDeviceFunction(device, vendor_type, CH32RV_USB_VENDOR_NAME), _endpointSize(clamp_ep(endpointSize))
{
    if (attached()) {
        vendors[instance()] = this;
        ch32rv_usb_platform_use();
        ch32rv_usb_platform.vendorControl = vendor_request;
    }
}

/* ---------------------------------------------------------- the stack's side */
void CH32RVUsbDeviceVendor::usbReset()
{
    if (_direct) {
        /* end() has cleared started() by now; a bus reset has not. */
        directEnded(device().started() ? CH32RVUsbTransferStatus::BusReset : CH32RVUsbTransferStatus::Stopped, 0,
                    false);
    }
    _outBusy = _inBusy = _zlp = false;
    _rxHeld = 0;
}

void CH32RVUsbDeviceVendor::usbConfigured(CH32RVUsbSpeed speed)
{
    _packet = speed == CH32RVUsbSpeed::Full ? _endpointSize : ch32rv_usb_bulk_size(speed);
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
    receive();
}

uint16_t CH32RVUsbDeviceVendor::rxUsed() const
{
    return _rxEnd ? (uint16_t)(_rxEnd - _rxTail + _rxHead) : (uint16_t)(_rxHead - _rxTail);
}

void CH32RVUsbDeviceVendor::receive()
{
    if (_outBusy || _rxHeld || !mounted()) {
        return;
    }
    if (_onRxData) {
        /* Nothing is kept: each transfer takes the whole buffer, whole packets
         * of it, and ends early on a short packet. */
        _rxHead = _rxTail = _rxEnd = 0;
        if (_rxSize < _packet) {
            return;
        }
        _outBusy = device().transfer(_ep, _rx, (uint16_t)(_rxSize / _packet * _packet));
        _rxAt = _rx;
        return;
    }
    if (!_rxEnd && _rxHead == _rxTail) {
        _rxHead = _rxTail = 0;
    }
    if (!_rxEnd && (uint16_t)(_rxSize - _rxHead) < _packet) {
        if (_rxTail <= _packet) {
            return;
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

void CH32RVUsbDeviceVendor::push()
{
    if (_inBusy || !mounted()) {
        return;
    }
    if (_txHead == _txTail) {
        if (_zlp && device().transfer((uint8_t)(0x80 | _ep), _tx, 0)) {
            _zlp = false;
            _inBusy = true;
            _txSending = 0;
        }
        return;
    }
    _txSending = _txHead > _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(_txSize - _txTail);
    _inBusy = device().transfer((uint8_t)(0x80 | _ep), _tx + _txTail, _txSending);
}

void CH32RVUsbDeviceVendor::usbTransferred(uint8_t address, uint16_t length)
{
    if (address == _ep) {
        _outBusy = false;
        if (_onRxData) {
            /* Held where it landed (where this transfer was armed: one armed
             * before onRxData() was set lands at the FIFO's head) until
             * usbDeliver() hands it over; the next transfer reuses the room. */
            if (length) {
                _rxHeld = length;
            } else {
                receive();
            }
            return;
        }
        _rxHead = (uint16_t)(_rxHead + length);
        receive();
        if (length) {
            handleRx();
        }
    } else if (address == (uint8_t)(0x80 | _ep)) {
        _inBusy = false;
        if (_direct) {
            _directOut.actualBytes += length;
            if (_directZlp && device().transfer((uint8_t)(0x80 | _ep), _tx, 0)) {
                _directZlp = false;                  /* EndWithShortPacket */
                _inBusy = true;
                return;
            }
            directEnded(CH32RVUsbTransferStatus::Success, _directOut.actualBytes, true);
            push();
            return;
        }
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

bool CH32RVUsbDeviceVendor::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    return (setup.bmRequestType & 0x60) == 0x40 && handleControlRequest(stage, setup);
}

/* ---------------------------------------------------------- the API */
bool CH32RVUsbDeviceVendor::mounted() const
{
    return attached() && device().ready();
}

int CH32RVUsbDeviceVendor::available()
{
    ch32rv_usb_task();
    return rxUsed();
}

int CH32RVUsbDeviceVendor::read()
{
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

int CH32RVUsbDeviceVendor::peek()
{
    ch32rv_usb_task();
    return rxUsed() ? _rx[_rxTail] : -1;
}

size_t CH32RVUsbDeviceVendor::read(uint8_t *buffer, size_t size)
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

size_t CH32RVUsbDeviceVendor::write(uint8_t data)
{
    return write(&data, 1);
}

/* Takes what fits now, as EspUsbDevice does: the return value says how much. */
size_t CH32RVUsbDeviceVendor::write(const uint8_t *buffer, size_t size)
{
    if (!mounted() || !buffer) {
        return 0;
    }
    size_t n = 0;
    while (n < size && writeAvailable()) {
        _tx[_txHead] = buffer[n++];
        _txHead = (uint16_t)(_txHead + 1 == _txSize ? 0 : _txHead + 1);
    }
    push();
    ch32rv_usb_task();
    return n;
}

size_t CH32RVUsbDeviceVendor::writeAvailable() const
{
    if (!mounted()) {
        return 0;
    }
    const uint16_t used = _txHead >= _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(_txSize - _txTail + _txHead);
    return (size_t)(_txSize - 1 - used);
}

bool CH32RVUsbDeviceVendor::configure(const CH32RVUsbBulkStreamConfig &config)
{
    uint8_t *rx, *tx;
    uint16_t rxSize, txSize;
    if (device().started() ||
        !ch32rv_usb_bulk_buffer(config.rxBuffer, config.rxCapacity, _rxStore, sizeof _rxStore, rx, rxSize) ||
        !ch32rv_usb_bulk_buffer(config.txBuffer, config.txCapacity, _txStore, sizeof _txStore, tx, txSize)) {
        return false;
    }
    _rx = rx;
    _rxSize = rxSize;
    _tx = tx;
    _txSize = txSize;
    _terminate = config.streamTermination == CH32RVUsbStreamTermination::EndWithShortPacket;
    _rxHead = _rxTail = _rxEnd = 0;
    _txHead = _txTail = 0;
    return true;
}

bool CH32RVUsbDeviceVendor::waitWritable(size_t bytes, uint32_t timeoutMs)
{
    if (bytes > writeCapacity()) {
        return false;
    }
    flush();
    const uint32_t t0 = millis();
    for (;;) {
        if (!mounted()) {
            return false;
        }
        if (writeAvailable() >= bytes) {
            return true;
        }
        if ((uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
        ch32rv_usb_task();
    }
}

void CH32RVUsbDeviceVendor::flush()
{
    push();
    ch32rv_usb_task();
}

/* Answer at stage SETUP: data, or a status stage when none. */
bool CH32RVUsbDeviceVendor::sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request,
                                                const void *data, size_t length)
{
    if (request.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
        return false;
    }
    if (!data || !length) {
        return device().controlStatus();
    }
    return device().controlIn(data, (uint16_t)length);
}

void CH32RVUsbDeviceVendor::handleRx()
{
    _rxPending = true;
}

void CH32RVUsbDeviceVendor::handleTxComplete(size_t sentBytes)
{
    if (_onTxComplete) {
        _txDone += sentBytes;
    }
}

size_t CH32RVUsbDeviceVendor::usbDeliver()
{
    size_t ran = 0;
    if (_rxHeld) {
        /* Still held during the call: nothing re-arms the endpoint over it. */
        if (_onRxData) {
            _onRxData(_rxAt, _rxHeld);
            ran++;
        }
        _rxHeld = 0;
        receive();
    }
    if (_rxPending) {
        _rxPending = false;
        if (_onRx && rxUsed()) {
            _onRx(rxUsed());
            ran++;
        }
    }
    if (_directDone) {
        /* Cleared first: the callback may arm the next one. */
        const CH32RVUsbTransferResult result = _directOut;
        _directDone = false;
        if (_onDirectTx) {
            _onDirectTx(result);
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

bool CH32RVUsbDeviceVendor::handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
{
    if (!_onControl) {
        return false;
    }
    CH32RVUsbDeviceVendorControlRequest r;
    r.stage = stage == CH32RVUsbControlStage::Setup ? CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP
              : stage == CH32RVUsbControlStage::Data ? CH32RV_USB_DEVICE_CONTROL_STAGE_DATA
                                                     : CH32RV_USB_DEVICE_CONTROL_STAGE_ACK;
    r.bmRequestType = setup.bmRequestType;
    r.bRequest = setup.bRequest;
    r.wValue = setup.wValue;
    r.wIndex = setup.wIndex;
    r.wLength = setup.wLength;
    r.actualBytes = device().controlActualBytes();
    r.rawRequest = &setup;
    return _onControl(r);
}
