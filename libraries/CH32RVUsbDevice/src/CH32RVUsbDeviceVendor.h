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
    /* Data-stage bytes so far: 0 at SETUP, the actual total at DATA and ACK
     * (either direction; no ZLP or status packet). */
    uint16_t actualBytes = 0;
    const void *rawRequest = nullptr;
};

class CH32RVUsbDeviceVendor : public CH32RVUsbDeviceBulkStream, public Print {
public:
    /* A vendor request the library does not answer itself. Return true when
     * handled: answer it at stage SETUP with sendControlResponse(), and
     * return true at the later stages. */
    typedef CH32RVCallback<bool(const CH32RVUsbDeviceVendorControlRequest &request)> ControlRequestCallback;

    /* Full speed: bulk endpoints are 8, 16, 32 or 64 bytes. */
    explicit CH32RVUsbDeviceVendor(CH32RVUsbDevice &device = USBDevice, uint16_t endpointSize = 64);

    /* The FIFOs, borrowed transfers and callbacks are CH32RVUsbDeviceBulkStream's;
     * Vendor's default streamTermination sends no zero-length packet. */
    int available() { return bulkAvailable(); }
    int read();
    size_t read(uint8_t *buffer, size_t size) { return bulkRead(buffer, size); }
    /* The next byte read() would return, left in place; -1 when none. */
    int peek() { return bulkPeek(); }
    size_t write(uint8_t data) override;
    /* Takes what fits now, as EspUsbDevice does: the return value says how much. */
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER
    /* The room for Print (whose own answer is 0, which stalls code that waits for room). */
    int availableForWrite() override { return (int)writeAvailable(); }
    /* Wait until the FIFO has room for `bytes`, running the stack meanwhile.
     * Flushes first. False on the timeout and for more than writeCapacity(). */
    bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
    /* Send what is in the FIFO now. write() already starts sending; this runs
     * the stack once more. */
    void flush() override;

    /* A provider: called inside the USB service while the host waits. */
    void onControlRequest(ControlRequestCallback cb) { _onControl = cb; }
    /* Answer a request at stage SETUP: data, or a status stage when none. */
    bool sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request, const void *data = nullptr,
                             size_t length = 0);
    uint16_t endpointSize() const { return _endpointSize; }
    /* bInterfaceSubClass / bInterfaceProtocol (class stays 0xFF), for a
     * device a host finds by them - an OEP probe is 0xFF / 0x4F / 0x45.
     * Before begin(). */
    void setInterfaceProtocol(uint8_t subclass, uint8_t protocol)
    {
        _subclass = subclass;
        _protocol = protocol;
    }
    uint8_t interfaceSubclass() const { return _subclass; }
    uint8_t interfaceProtocol() const { return _protocol; }

    /* For the USB stack's callbacks. */
    bool handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);

protected:
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;

private:
    uint8_t _subclass = 0, _protocol = 0;
    uint16_t _endpointSize;
    ControlRequestCallback _onControl = nullptr;
};
