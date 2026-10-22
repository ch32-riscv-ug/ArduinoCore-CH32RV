/* A CDC-ACM serial port (a virtual COM port) behind Arduino's Stream.
 * Included by CH32RVUsbDevice.h. */
#pragma once

class CH32RVUsbDeviceCdcSerial : public Stream, public CH32RVUsbDeviceFunction {
public:
    typedef CH32RVCallback<void(const CH32RVUsbDeviceCdcLineCoding &coding)> LineCodingCallback;
    typedef CH32RVCallback<void(const CH32RVUsbDeviceCdcLineState &state)> LineStateCallback;
    typedef CH32RVCallback<void(size_t available)> RxCallback;

    /* `name` is what the host shows for this port; give each port of a
     * multi-port device its own (EspUsbDeviceCdcSerial). */
    explicit CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device = USBDevice, const char *name = nullptr);

    /* Starts the device with its default configuration if the sketch has not
     * begun it, so SerialUSB.begin() alone is enough. The baud rate is the
     * host's business; the argument exists for Serial compatibility. */
    void begin(unsigned long baud = 115200);
    void end() {}
    /* Use the sketch's buffers instead of the built-in ones (twice the
     * largest packet each: 256 bytes on a full-speed part). The receive
     * buffer needs room for two packets at the speed the host picks (128 /
     * 1024 / 2048 bytes); a smaller one cannot take data at that speed.
     * Before begin(). */
    void setBuffers(uint8_t *rx, uint16_t rxSize, uint8_t *tx, uint16_t txSize);

    int available() override;
    int peek() override;
    int read() override;
    size_t read(uint8_t *buffer, size_t size);
    int availableForWrite() override;
    void flush() override;
    size_t write(uint8_t c) override;
    /* Blocks while a host has the port open and the buffer is full; with no
     * host (or once it closes the port) the bytes are dropped instead, like an
     * unconnected UART, so a sketch that prints never hangs on a missing
     * cable. */
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER

    /* True while a host has the port open (DTR asserted). */
    operator bool();
    bool connected();
    /* This port's index among the device's CDC ports. */
    uint8_t port() const { return instance(); }
    const CH32RVUsbDeviceCdcLineCoding &lineCoding() const { return _lineCoding; }
    const CH32RVUsbDeviceCdcLineState &lineState() const { return _lineState; }
    bool dtr();
    bool rts();
    /* The line coding the host last set. */
    uint32_t baud();

    /* The Arduino "1200 bps touch": a host that opens the port at 1200 baud
     * and closes it resets the chip into its factory USB bootloader. On by
     * default. */
    void setBootTouchEnabled(bool enabled) { _bootTouch = enabled; }
    bool bootTouchEnabled() const { return _bootTouch; }

    /* When the host changes DTR/RTS or the line coding, or data arrives: run
     * from deliverEvents(), with the state at that time. Several changes
     * before a delivery arrive as one; onRx() not at all when the data was
     * read before it. */
    void onLineState(LineStateCallback cb) { _onLineState = cb; }
    void onLineCoding(LineCodingCallback cb) { _onLineCoding = cb; }
    void onRx(RxCallback cb) { _onRx = cb; }

    /* For the USB stack's callbacks. */
    void handleLineCoding(uint32_t baud, uint8_t stopBits, uint8_t parity, uint8_t dataBits);
    void handleLineState(bool dtr, bool rts);
    void handleRx();

    /* Endpoints, set while the descriptors are laid out. */
    uint8_t _epNotify = 0, _epData = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    size_t usbDeliver() override;

private:
    void receive();
    void push();
    uint16_t rxUsed() const;
    uint8_t _rxStore[CH32RV_USB_MAX_PACKET < 128 ? 256 : 2 * CH32RV_USB_MAX_PACKET];
    uint8_t _txStore[CH32RV_USB_MAX_PACKET < 128 ? 256 : 2 * CH32RV_USB_MAX_PACKET];
    uint8_t *_rx = _rxStore, *_tx = _txStore;
    uint16_t _rxSize = sizeof _rxStore, _txSize = sizeof _txStore;
    /* Receive: OUT transfers land straight in the buffer, a packet's worth of
     * contiguous room at a time; _rxEnd marks where the data wraps early. */
    uint16_t _rxHead = 0, _rxTail = 0, _rxEnd = 0;
    uint16_t _txHead = 0, _txTail = 0, _txSending = 0;
    uint8_t _coding[7] = {0, 0, 0, 0, 0, 0, 8};
    uint16_t _packet = 64;
    bool _outBusy = false, _inBusy = false, _zlp = false;
    bool _bootTouch = true;
    uint8_t _pending = 0;                        /* notifications for usbDeliver() */
    CH32RVUsbDeviceCdcLineCoding _lineCoding;
    CH32RVUsbDeviceCdcLineState _lineState;
    LineStateCallback _onLineState = nullptr;
    LineCodingCallback _onLineCoding = nullptr;
    RxCallback _onRx = nullptr;
};

/* The predefined port on USBDevice (CH32RVUsbDeviceSerialUSB.cpp). */
extern CH32RVUsbDeviceCdcSerial SerialUSB;
