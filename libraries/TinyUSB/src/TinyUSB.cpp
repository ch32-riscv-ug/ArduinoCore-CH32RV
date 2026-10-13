/* SerialUSB: TinyUSB's CDC interface 0 behind Arduino's Stream. */
#include "TinyUSB.h"
#include "tusb.h"

extern "C" void ch32rv_tusb_start(void);

USBCDC SerialUSB;

static bool boot_touch_enabled = true;

void USBCDC::begin(unsigned long baud)
{
    (void)baud;
    ch32rv_tusb_start();
}

void USBCDC::end()
{
}

int USBCDC::available()
{
    ch32rv_usb_task();
    return (int)tud_cdc_available();
}

int USBCDC::peek()
{
    ch32rv_usb_task();
    uint8_t c;
    return tud_cdc_peek(&c) ? c : -1;
}

int USBCDC::read()
{
    ch32rv_usb_task();
    return tud_cdc_available() ? (int)tud_cdc_read_char() : -1;
}

int USBCDC::availableForWrite()
{
    ch32rv_usb_task();
    return (int)tud_cdc_write_available();
}

void USBCDC::flush()
{
    tud_cdc_write_flush();
    ch32rv_usb_task();
}

size_t USBCDC::write(uint8_t c)
{
    return write(&c, 1);
}

/* Blocks while a host has the port open and the FIFO is full; with no host
 * (or once it closes the port) the bytes are dropped instead, like an
 * unconnected UART, so a sketch that prints never hangs on a missing cable. */
size_t USBCDC::write(const uint8_t *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ch32rv_usb_task();
        if (!tud_cdc_connected()) {
            break;
        }
        const uint32_t n = tud_cdc_write(buf + done, (uint32_t)(size - done));
        done += n;
        if (n == 0) {
            tud_cdc_write_flush();
        }
    }
    tud_cdc_write_flush();
    return done;
}

USBCDC::operator bool()
{
    ch32rv_usb_task();
    return tud_cdc_connected();
}

bool USBCDC::dtr()
{
    return (tud_cdc_get_line_state() & 0x01) != 0;
}

bool USBCDC::rts()
{
    return (tud_cdc_get_line_state() & 0x02) != 0;
}

uint32_t USBCDC::baud()
{
    cdc_line_coding_t coding;
    tud_cdc_get_line_coding(&coding);
    return coding.bit_rate;
}

void USBCDC::setBootTouchEnabled(bool enabled)
{
    boot_touch_enabled = enabled;
}

/* The 1200 bps touch: the host opens the port at 1200 baud and drops DTR.
 * Acting on the DTR drop (not on the baud change) lets the host finish its
 * own close before the device goes away. */
extern "C" void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;
    if (!dtr && boot_touch_enabled && SerialUSB.baud() == 1200) {
        TinyUSBDevice::enterBootloader();
    }
}
