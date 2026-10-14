/* A CDC-ACM serial port (a virtual COM port) behind Arduino's Stream.
 * Included by CH32RVUsbDevice.h. */
#pragma once

class CH32RVUsbDeviceCdcSerial : public Stream, public CH32RVUsbDeviceFunction {
public:
    explicit CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device = USBDevice);

    /* Starts the device with its default configuration if the sketch has not
     * begun it, so SerialUSB.begin() alone is enough. The baud rate is the
     * host's business; the argument exists for Serial compatibility. */
    void begin(unsigned long baud = 115200);
    void end() {}

    int available() override;
    int peek() override;
    int read() override;
    int availableForWrite() override;
    void flush() override;
    size_t write(uint8_t c) override;
    /* Blocks while a host has the port open and the buffer is full; with no
     * host (or once it closes the port) the bytes are dropped instead, like an
     * unconnected UART, so a sketch that prints never hangs on a missing
     * cable. */
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;

    /* True while a host has the port open (DTR asserted). */
    operator bool();
    bool dtr();
    bool rts();
    /* The line coding the host last set. */
    uint32_t baud();

    /* The Arduino "1200 bps touch": a host that opens the port at 1200 baud
     * and closes it resets the chip into its factory USB bootloader. On by
     * default. */
    void setBootTouchEnabled(bool enabled) { _bootTouch = enabled; }
    bool bootTouchEnabled() const { return _bootTouch; }

    /* Called from the USB stack (inside loop()/delay(), not an interrupt)
     * when the host changes DTR/RTS or the line coding. */
    typedef void (*LineStateCallback)(bool dtr, bool rts);
    typedef void (*LineCodingCallback)(uint32_t baud);
    void onLineState(LineStateCallback cb) { _onLineState = cb; }
    void onLineCoding(LineCodingCallback cb) { _onLineCoding = cb; }

    /* For the USB stack's callbacks. */
    void lineStateChanged(bool dtr, bool rts);
    void lineCodingChanged(uint32_t baud);

private:
    bool _bootTouch = true;
    LineStateCallback _onLineState = nullptr;
    LineCodingCallback _onLineCoding = nullptr;
};

/* The predefined port on USBDevice (CH32RVUsbDeviceSerialUSB.cpp). */
extern CH32RVUsbDeviceCdcSerial SerialUSB;
