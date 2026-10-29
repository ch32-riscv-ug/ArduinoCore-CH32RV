/* A CDC-ACM serial port (a virtual COM port) behind Arduino's Stream.
 * Included by CH32RVUsbDevice.h. */
#pragma once

class CH32RVUsbDeviceCdcSerial : public Stream, public CH32RVUsbDeviceBulkStream {
public:
    typedef CH32RVCallback<void(const CH32RVUsbDeviceCdcLineCoding &coding)> LineCodingCallback;
    typedef CH32RVCallback<void(const CH32RVUsbDeviceCdcLineState &state)> LineStateCallback;

    /* `name` is what the host shows for this port; give each port of a
     * multi-port device its own (EspUsbDeviceCdcSerial). */
    explicit CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device = USBDevice, const char *name = nullptr);

    /* Starts the device with its default configuration if the sketch has not
     * begun it, so SerialUSB.begin() alone is enough. The baud rate is the
     * host's business; the argument exists for Serial compatibility. */
    void begin(unsigned long baud = 115200);
    void end() {}
    /* The buffers, borrowed transfers and onRx() / onRxData() /
     * onTxComplete() are CH32RVUsbDeviceBulkStream's; CDC's default
     * streamTermination ends write()'s data with a zero-length packet after
     * whole packets. */

    int available() override { return bulkAvailable(); }
    int peek() override { return bulkPeek(); }
    int read() override;
    size_t read(uint8_t *buffer, size_t size) { return bulkRead(buffer, size); }
    int availableForWrite() override;
    /* Wait until `bytes` fit, running the stack meanwhile. False when the
     * port closes, on the timeout, and for more than writeCapacity(). */
    bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
    /* Waits until the queued bytes are sent (Serial's flush()), while a host
     * has the port open, at most the write timeout. */
    void flush() override;
    size_t write(uint8_t c) override;
    /* Waits for room while a host has the port open and the buffer is full -
     * as Serial does, so print() loses nothing - but at most the write
     * timeout without progress; then, and with no host (or once it closes the
     * port), the rest is dropped, like an unconnected UART. The return value
     * is what was queued. */
    size_t write(const uint8_t *buf, size_t size) override;
    /* How long write() and flush() wait for the host to take data (1000 ms;
     * 0 never waits). */
    void setTxTimeoutMs(uint32_t ms) { _txTimeoutMs = ms; }
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER

    /* True while a host has the port open (DTR asserted). */
    operator bool();
    bool connected();
    /* This port's index among the device's CDC ports. */
    uint8_t port() const { return instance(); }
    /* What the host last set (copies: they do not change under the caller). */
    CH32RVUsbDeviceCdcLineCoding lineCoding() const { return _lineCoding; }
    CH32RVUsbDeviceCdcLineState lineState() const { return _lineState; }
    bool dtr();
    bool rts();
    /* The line coding the host last set. */
    uint32_t baud();

    /* The Arduino "1200 bps touch": a host that opens the port at 1200 baud
     * and closes it resets the chip into its factory USB bootloader. On by
     * default. */
    void setBootTouchEnabled(bool enabled) { _bootTouch = enabled; }
    bool bootTouchEnabled() const { return _bootTouch; }

    /* When the host changes DTR/RTS or the line coding: run from
     * deliverEvents(), with the state at that time. Several changes before a
     * delivery arrive as one. */
    void onLineState(LineStateCallback cb) { _onLineState = cb; deliverWith(deliverThis); }
    void onLineCoding(LineCodingCallback cb) { _onLineCoding = cb; deliverWith(deliverThis); }

    /* For the USB stack's callbacks. */
    void handleLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity, uint8_t dataBits);
    void handleLineState(bool dtr, bool rts);

    /* The notification endpoint, set while the descriptors are laid out. */
    uint8_t _epNotify = 0;

protected:
    void usbReset() override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    size_t usbDeliver() override;

private:
    uint8_t _coding[7] = {0, 0, 0, 0, 0, 0, 8};
    uint32_t _txTimeoutMs = 1000;
    bool _bootTouch = true;
    uint8_t _pending = 0;                        /* notifications for usbDeliver() */
    CH32RVUsbDeviceCdcLineCoding _lineCoding;
    CH32RVUsbDeviceCdcLineState _lineState;
    LineStateCallback _onLineState = nullptr;
    LineCodingCallback _onLineCoding = nullptr;
};

/* The predefined port on USBDevice (CH32RVUsbDeviceSerialUSB.cpp). */
extern CH32RVUsbDeviceCdcSerial SerialUSB;
