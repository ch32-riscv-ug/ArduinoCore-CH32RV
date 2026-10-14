/* CDC-ACM serial: TinyUSB's cdc_device behind Stream. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t cdc_driver = {
    NULL, cdcd_init, cdcd_deinit, cdcd_reset, cdcd_open, cdcd_control_xfer_cb, cdcd_xfer_cb, NULL, NULL,
};

/* Notification IN on endpoint n, data OUT and IN on n + 1. */
static uint16_t cdc_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint8_t itf,
                               uint8_t *ep, uint8_t str)
{
    (void)self;
    const uint8_t n = *ep;
    *ep = (uint8_t)(n + 2);
    const uint8_t d[] = {TUD_CDC_DESCRIPTOR(itf, str, (uint8_t)(0x80 | n), 8, (uint8_t)(n + 1),
                                            (uint8_t)(0x80 | (n + 1)), CFG_TUD_CDC_EP_BUFSIZE)};
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType cdc_type = {&cdc_driver, cdc_descriptor, 2, CFG_TUD_CDC};

/* TinyUSB reports by instance; this finds the object. */
static CH32RVUsbDeviceCdcSerial *ports[CFG_TUD_CDC];

CH32RVUsbDeviceCdcSerial::CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, cdc_type, "CDC")
{
    if (attached()) {
        ports[instance()] = this;
    }
}

void CH32RVUsbDeviceCdcSerial::begin(unsigned long baud)
{
    (void)baud;
    device().begin();
}

int CH32RVUsbDeviceCdcSerial::available()
{
    ch32rv_usb_task();
    return (int)tud_cdc_n_available(instance());
}

int CH32RVUsbDeviceCdcSerial::peek()
{
    ch32rv_usb_task();
    uint8_t c;
    return tud_cdc_n_peek(instance(), &c) ? c : -1;
}

int CH32RVUsbDeviceCdcSerial::read()
{
    ch32rv_usb_task();
    return tud_cdc_n_available(instance()) ? (int)tud_cdc_n_read_char(instance()) : -1;
}

int CH32RVUsbDeviceCdcSerial::availableForWrite()
{
    ch32rv_usb_task();
    return (int)tud_cdc_n_write_available(instance());
}

void CH32RVUsbDeviceCdcSerial::flush()
{
    tud_cdc_n_write_flush(instance());
    ch32rv_usb_task();
}

size_t CH32RVUsbDeviceCdcSerial::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32RVUsbDeviceCdcSerial::write(const uint8_t *buf, size_t size)
{
    size_t done = 0;
    while (done < size) {
        ch32rv_usb_task();
        if (!tud_cdc_n_connected(instance())) {
            break;
        }
        const uint32_t n = tud_cdc_n_write(instance(), buf + done, (uint32_t)(size - done));
        done += n;
        if (n == 0) {
            tud_cdc_n_write_flush(instance());
        }
    }
    tud_cdc_n_write_flush(instance());
    return done;
}

CH32RVUsbDeviceCdcSerial::operator bool()
{
    ch32rv_usb_task();
    return tud_cdc_n_connected(instance());
}

bool CH32RVUsbDeviceCdcSerial::dtr()
{
    return (tud_cdc_n_get_line_state(instance()) & 0x01) != 0;
}

bool CH32RVUsbDeviceCdcSerial::rts()
{
    return (tud_cdc_n_get_line_state(instance()) & 0x02) != 0;
}

uint32_t CH32RVUsbDeviceCdcSerial::baud()
{
    cdc_line_coding_t coding;
    tud_cdc_n_get_line_coding(instance(), &coding);
    return coding.bit_rate;
}

/* The 1200 bps touch: the host opens the port at 1200 baud and drops DTR.
 * Acting on the DTR drop (not on the baud change) lets the host finish its
 * own close before the device goes away. */
void CH32RVUsbDeviceCdcSerial::lineStateChanged(bool dtr, bool rts)
{
    if (!dtr && _bootTouch && baud() == 1200) {
        CH32RVUsbDevice::requestBootloader();
    }
    if (_onLineState) {
        _onLineState(dtr, rts);
    }
}

void CH32RVUsbDeviceCdcSerial::lineCodingChanged(uint32_t baud)
{
    if (_onLineCoding) {
        _onLineCoding(baud);
    }
}

extern "C" void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    if (itf < CFG_TUD_CDC && ports[itf]) {
        ports[itf]->lineStateChanged(dtr, rts);
    }
}

extern "C" void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding)
{
    if (itf < CFG_TUD_CDC && ports[itf]) {
        ports[itf]->lineCodingChanged(coding->bit_rate);
    }
}

#else /* !CH32RV_TINYUSB_DEVICE: a port that never opens */

static uint16_t cdc_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType cdc_type = {nullptr, cdc_descriptor, 2, 1};

CH32RVUsbDeviceCdcSerial::CH32RVUsbDeviceCdcSerial(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceFunction(device, cdc_type, "CDC")
{
}

void CH32RVUsbDeviceCdcSerial::begin(unsigned long baud)
{
    (void)baud;
    device().begin();
}

int CH32RVUsbDeviceCdcSerial::available() { return 0; }
int CH32RVUsbDeviceCdcSerial::peek() { return -1; }
int CH32RVUsbDeviceCdcSerial::read() { return -1; }
int CH32RVUsbDeviceCdcSerial::availableForWrite() { return 0; }
void CH32RVUsbDeviceCdcSerial::flush() {}
size_t CH32RVUsbDeviceCdcSerial::write(uint8_t) { return 0; }
size_t CH32RVUsbDeviceCdcSerial::write(const uint8_t *, size_t) { return 0; }
CH32RVUsbDeviceCdcSerial::operator bool() { return false; }
bool CH32RVUsbDeviceCdcSerial::dtr() { return false; }
bool CH32RVUsbDeviceCdcSerial::rts() { return false; }
uint32_t CH32RVUsbDeviceCdcSerial::baud() { return 0; }
void CH32RVUsbDeviceCdcSerial::lineStateChanged(bool, bool) {}
void CH32RVUsbDeviceCdcSerial::lineCodingChanged(uint32_t) {}

#endif
