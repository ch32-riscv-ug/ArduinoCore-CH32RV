/* USB device for sketches: a CDC serial port (SerialUSB) on the chip's own USB,
 * driven by the vendored TinyUSB (ADR-0012).
 *
 *   #include <TinyUSB.h>
 *   void setup() { SerialUSB.begin(); }
 *   void loop()  { SerialUSB.println("hello"); delay(1000); }
 *
 * The stack runs from ch32rv_usb_task(): after every loop(), inside delay(),
 * and in each SerialUSB call. A sketch that blocks elsewhere for a long time
 * starves it; the host then sees a stalled port, not a lost one.
 *
 * Opening the port at 1200 baud and closing it (the Arduino "1200 bps touch")
 * puts the chip into its factory USB bootloader, where ch32rv's `isp` writes
 * it. With no upload the bootloader returns to the application after about
 * 60 s. setBootTouchEnabled(false) turns this off. */
#pragma once

#include <Arduino.h>

class USBCDC : public Stream {
public:
    /* Starts the USB device (first call only) and the CDC port. The baud rate
     * is the host's business; the argument exists for Serial compatibility. */
    void begin(unsigned long baud = 115200);
    void end();

    int available() override;
    int peek() override;
    int read() override;
    int availableForWrite() override;
    void flush() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;

    /* True while a host has the port open (DTR asserted). */
    operator bool();
    bool dtr();
    bool rts();
    /* The line coding the host last set. */
    uint32_t baud();

    void setBootTouchEnabled(bool enabled);
};

extern USBCDC SerialUSB;

/* The USB device as a whole. */
namespace TinyUSBDevice {
/* True once the host has configured the device. */
bool mounted();
/* True while the bus is suspended. */
bool suspended();
/* Detach from the bus, then reset into the factory USB bootloader (series
 * with a software-selected boot area only). Does not return. */
[[noreturn]] void enterBootloader();
}
