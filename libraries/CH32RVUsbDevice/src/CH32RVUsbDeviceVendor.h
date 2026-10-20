/* Vendor-specific interface with a bulk OUT/IN endpoint pair
 * (EspUsbDeviceVendor): a raw byte pipe that libusb, WebUSB (Chrome) or
 * WinUSB reach with no driver install - Windows gets its WinUSB binding from
 * the Microsoft OS 2.0 descriptors, which come with this class. Included by
 * CH32RVUsbDevice.h. */
#pragma once

/* CH32RVUsbDeviceVendorControlRequest::stage: setup, data, status. */
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP = 1;
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_DATA = 2;
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_ACK = 3;

struct CH32RVUsbDeviceVendorControlRequest {
    uint8_t rhport = 0;
    uint8_t stage = 0;
    uint8_t bmRequestType = 0;
    uint8_t bRequest = 0;
    uint16_t wValue = 0;
    uint16_t wIndex = 0;
    uint16_t wLength = 0;
    const void *rawRequest = nullptr;
};

/* EspUsbDeviceVendorDirectError. The USBFS and USBD drivers always copy
 * through their own endpoint buffers, so writeDirect() reports NotSupported on
 * every series for now - USBHS included, whose DMA path is not wired to it. */
enum class CH32RVUsbDeviceVendorDirectError : uint8_t {
    None = 0,
    NotSupported,
    NotMounted,
    BadArgument,
    NotAligned,
    NotDmaCapable,
    Busy,
    TransferFailed,
};

class CH32RVUsbDeviceVendor : public CH32RVUsbDeviceFunction, public Print {
public:
    /* callback(bytes available) when data arrives. */
    typedef void (*RxCallback)(size_t available);
    /* callback(sentBytes) when a transmit on the bulk IN endpoint completed. */
    typedef void (*TxCompleteCallback)(size_t sentBytes);
    typedef void (*RxDataCallback)(const uint8_t *data, size_t length);
    /* A vendor request the library does not answer itself. Return true when
     * handled: answer it at stage SETUP with sendControlResponse(), and
     * return true at the later stages. */
    typedef bool (*ControlRequestCallback)(const CH32RVUsbDeviceVendorControlRequest &request);

    /* Full speed: bulk endpoints are 8, 16, 32 or 64 bytes. */
    explicit CH32RVUsbDeviceVendor(CH32RVUsbDevice &device = USBDevice, uint16_t endpointSize = 64);

    bool mounted() const;
    int available();
    int read();
    size_t read(uint8_t *buffer, size_t size);
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER
    /* Room in the transmit FIFO now; write() never takes more. */
    size_t writeAvailable() const;
    /* The transmit buffer's size (twice the largest packet, at least 256). */
    static size_t writeCapacity();
    /* Wait until the FIFO has room for `bytes` (clamped to writeCapacity()),
     * running the stack meanwhile. Flushes first. */
    bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
    /* Send what is in the FIFO now, whole packet or not: needed after a write
     * shorter than the endpoint size. */
    void flush() override;

    /* EspUsbDevice's zero-copy path. Not on USBFS yet: false / NotSupported. */
    static bool directWriteSupported() { return false; }
    bool writeDirect(const void *buffer, size_t length);
    CH32RVUsbDeviceVendorDirectError lastDirectError() const { return _directError; }
    const char *lastDirectErrorName() const;

    void onRx(RxCallback cb) { _onRx = cb; }
    void onTxComplete(TxCompleteCallback cb) { _onTxComplete = cb; }
    /* Direct builds only in EspUsbDevice; never called here. */
    void onRxData(RxDataCallback cb) { _onRxData = cb; }
    void onControlRequest(ControlRequestCallback cb) { _onControl = cb; }
    /* Answer a request at stage SETUP: data, or a status stage when none. */
    bool sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request, const void *data = nullptr,
                             size_t length = 0);
    uint16_t endpointSize() const { return _endpointSize; }

    /* For the USB stack's callbacks. */
    void handleRx();
    void handleTxComplete(size_t sentBytes);
    bool handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);

    /* The endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;

private:
    static constexpr uint16_t BUFFER = CH32RV_USB_MAX_PACKET < 128 ? 256 : 2 * CH32RV_USB_MAX_PACKET;
    void receive();
    void push();
    uint16_t rxUsed() const;
    uint8_t _rx[BUFFER], _tx[BUFFER];
    /* As CH32RVUsbDeviceCdcSerial: OUT straight into _rx, wrapping early at _rxEnd. */
    uint16_t _rxHead = 0, _rxTail = 0, _rxEnd = 0;
    uint16_t _txHead = 0, _txTail = 0, _txSending = 0;
    uint16_t _packet = 64;
    bool _outBusy = false, _inBusy = false, _zlp = false;
    uint16_t _endpointSize;
    RxCallback _onRx = nullptr;
    TxCompleteCallback _onTxComplete = nullptr;
    RxDataCallback _onRxData = nullptr;
    ControlRequestCallback _onControl = nullptr;
    CH32RVUsbDeviceVendorDirectError _directError = CH32RVUsbDeviceVendorDirectError::None;
};
