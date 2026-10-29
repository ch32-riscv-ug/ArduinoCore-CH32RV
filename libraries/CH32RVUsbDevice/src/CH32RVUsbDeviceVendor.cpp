/* Vendor (bulk) on the device stack, buffered. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>


const char *CH32RVUsbDeviceVendor::lastDirectErrorName() const
{
    switch (_directError) {
    case CH32RVUsbDeviceVendorDirectError::None: return "None";
    case CH32RVUsbDeviceVendorDirectError::NotSupported: return "NotSupported";
    case CH32RVUsbDeviceVendorDirectError::NotMounted: return "NotMounted";
    case CH32RVUsbDeviceVendorDirectError::BadArgument: return "BadArgument";
    case CH32RVUsbDeviceVendorDirectError::NotAligned: return "NotAligned";
    case CH32RVUsbDeviceVendorDirectError::NotDmaCapable: return "NotDmaCapable";
    case CH32RVUsbDeviceVendorDirectError::Busy: return "Busy";
    case CH32RVUsbDeviceVendorDirectError::TransferFailed: return "TransferFailed";
    }
    return "Unknown";
}

bool CH32RVUsbDeviceVendor::writeDirect(const void *buffer, size_t length)
{
    if (!buffer || !length || length > 65535u) {
        _directError = CH32RVUsbDeviceVendorDirectError::BadArgument;
        return false;
    }
    if (!mounted()) {
        _directError = CH32RVUsbDeviceVendorDirectError::NotMounted;
        return false;
    }
    if (_inBusy || _zlp || _txHead != _txTail) {
        _directError = CH32RVUsbDeviceVendorDirectError::Busy;
        return false;
    }
    if (!device().transfer((uint8_t)(0x80 | _ep), const_cast<void *>(buffer), (uint16_t)length)) {
        _directError = CH32RVUsbDeviceVendorDirectError::TransferFailed;
        return false;
    }
    _inBusy = _direct = true;
    _txSending = 0;
    _directError = CH32RVUsbDeviceVendorDirectError::None;
    return true;
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
    _outBusy = _inBusy = _zlp = _direct = false;
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
        uint8_t *at = _rx;
        uint16_t room = BUFFER;
        if (_rxData && _rxDataSize >= _packet) {
            at = _rxData;
            room = _rxDataSize;
        }
        _outBusy = device().transfer(_ep, at, (uint16_t)(room / _packet * _packet));
        _rxAt = at;
        return;
    }
    if (!_rxEnd && _rxHead == _rxTail) {
        _rxHead = _rxTail = 0;
    }
    if (!_rxEnd && (uint16_t)(BUFFER - _rxHead) < _packet) {
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
    _txSending = _txHead > _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(BUFFER - _txTail);
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
            _direct = false;
            handleTxComplete(length);
            push();
            return;
        }
        const uint16_t sent = _txSending;
        if (_txSending) {
            _txTail = (uint16_t)((_txTail + _txSending) % BUFFER);
            _zlp = _txHead == _txTail && (_txSending % _packet) == 0;
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
        _txHead = (uint16_t)(_txHead + 1 == BUFFER ? 0 : _txHead + 1);
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
    const uint16_t used = _txHead >= _txTail ? (uint16_t)(_txHead - _txTail) : (uint16_t)(BUFFER - _txTail + _txHead);
    return (size_t)(BUFFER - 1 - used);
}

size_t CH32RVUsbDeviceVendor::writeCapacity()
{
    return BUFFER - 1;
}

bool CH32RVUsbDeviceVendor::waitWritable(size_t bytes, uint32_t timeoutMs)
{
    if (bytes > writeCapacity()) {
        bytes = writeCapacity();
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
    r.rawRequest = &setup;
    return _onControl(r);
}
