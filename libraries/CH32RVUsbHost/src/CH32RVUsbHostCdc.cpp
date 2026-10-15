/* CH32RVUsbHost: CDC-ACM serial, and the FTDI / CP210x / CH34x / PL2303 USB
 * serial chips TinyUSB's CDC host driver also speaks. Ports are numbered per
 * device in the order TinyUSB mounted them. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_TINYUSB_HOST && CFG_TUH_CDC

namespace {

const usbh_class_driver_t cdc_driver = {
    "CDC", cdch_init, cdch_deinit, cdch_open, cdch_set_config, cdch_xfer_cb, cdch_close,
};

/* Received bytes go to onSerialData() and to the CH32RVUsbHostCdcSerial bound
 * to that port. With neither, they stay in TinyUSB's FIFO (the device is
 * NAKed once it fills). */
struct Rx {
    CH32RVUsbHostCdcSerial *owner;
    uint8_t buf[CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE];
    uint16_t head, tail;
};
Rx rx[CFG_TUH_CDC];
CH32RVUsbHost::SerialDataCallback data_cb;

void poll(CH32RVUsbHost &host);
CH32RVUsbHostGroup cdc_group = {&cdc_driver, poll, nullptr, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(cdc_group); }
} join;

uint16_t rx_count(const Rx &r)
{
    return (uint16_t)((r.head + sizeof r.buf - r.tail) % sizeof r.buf);
}

uint8_t daddr_of(uint8_t idx)
{
    tuh_itf_info_t info;
    return tuh_cdc_itf_get_info(idx, &info) ? info.daddr : 0;
}

/* TinyUSB's CDC index for (address, port); -1 when there is none. */
int index_of(uint8_t address, uint8_t port)
{
    uint8_t seen = 0;
    uint8_t first_dev = 0;
    for (uint8_t i = 0; i < CFG_TUH_CDC; i++) {
        if (!tuh_cdc_mounted(i)) {
            continue;
        }
        const uint8_t d = daddr_of(i);
        if (address == CH32RV_USB_HOST_ANY_ADDRESS) {
            if (!first_dev) {
                first_dev = d;
            }
            if (d != first_dev) {
                continue;
            }
        } else if (d != address) {
            continue;
        }
        if (port == CH32RV_USB_HOST_ANY_PORT || port == seen) {
            return i;
        }
        seen++;
    }
    return -1;
}

void poll(CH32RVUsbHost &host)
{
    (void)host;
    for (uint8_t i = 0; i < CFG_TUH_CDC; i++) {
        if (!tuh_cdc_mounted(i)) {
            continue;
        }
        Rx &r = rx[i];
        if (!data_cb && !r.owner) {
            continue;
        }
        uint8_t chunk[32];
        uint32_t room = r.owner ? (uint32_t)(sizeof r.buf - 1u - rx_count(r)) : sizeof chunk;
        while (room) {
            const uint32_t n = tuh_cdc_read(i, chunk, TU_MIN(room, (uint32_t)sizeof chunk));
            if (!n) {
                break;
            }
            if (r.owner) {
                for (uint32_t k = 0; k < n; k++) {
                    r.buf[r.head] = chunk[k];
                    r.head = (uint16_t)((r.head + 1u) % sizeof r.buf);
                }
                room -= n;
            }
            if (data_cb) {
                CH32RVUsbHostSerialData d;
                tuh_itf_info_t info;
                if (tuh_cdc_itf_get_info(i, &info)) {
                    d.address = info.daddr;
                    d.interfaceNumber = info.desc.bInterfaceNumber;
                }
                uint8_t port = 0;
                for (uint8_t j = 0; j < i; j++) {
                    port = (uint8_t)(port + (tuh_cdc_mounted(j) && daddr_of(j) == d.address));
                }
                d.port = port;
                d.data = chunk;
                d.length = n;
                data_cb(d);
            }
        }
    }
}

/* A CDC control request, waited for. */
struct Wait {
    volatile bool done;
    volatile xfer_result_t result;
    uint32_t generation;          /* a request given up on must not complete the next */
};
Wait wait_state;

void wait_done(tuh_xfer_t *xfer)
{
    if ((uint32_t)xfer->user_data != wait_state.generation) {
        return;
    }
    wait_state.result = xfer->result;
    wait_state.done = true;
}

bool settle(CH32RVUsbHost &host, bool started)
{
    if (!started) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    if (!ch32rv_usb_host_wait(wait_state.done, 2000)) {
        wait_state.generation++;
        return host.fail(CH32RVUsbHostError::Timeout);
    }
    if (wait_state.result != XFER_RESULT_SUCCESS) {
        return host.fail(ch32rv_usb_host_result_error(wait_state.result));
    }
    return host.ok();
}

uint32_t arm()
{
    wait_state.generation++;
    wait_state.done = false;
    wait_state.result = XFER_RESULT_INVALID;
    return wait_state.generation;
}

bool set_line_coding(CH32RVUsbHost &host, int idx, const CH32RVUsbHostSerialConfig &config)
{
    cdc_line_coding_t coding = {};
    coding.bit_rate = config.baud;
    coding.stop_bits = (uint8_t)config.stopBits;
    coding.parity = (uint8_t)config.parity;
    coding.data_bits = config.dataBits;
    const uint32_t g = arm();
    return settle(host, tuh_cdc_set_line_coding((uint8_t)idx, &coding, wait_done, g));
}

bool set_lines(CH32RVUsbHost &host, int idx, bool dtr, bool rts)
{
    const uint16_t state = (uint16_t)((dtr ? CDC_CONTROL_LINE_STATE_DTR : 0) |
                                      (rts ? CDC_CONTROL_LINE_STATE_RTS : 0));
    const uint32_t g = arm();
    return settle(host, tuh_cdc_set_control_line_state((uint8_t)idx, state, wait_done, g));
}

}  // namespace

extern "C" void tuh_cdc_mount_cb(uint8_t idx)
{
    if (idx < CFG_TUH_CDC) {
        rx[idx].head = rx[idx].tail = 0;
        tuh_itf_info_t info;
        if (tuh_cdc_itf_get_info(idx, &info)) {
            ch32rv_usb_host_mark_claimed(info.daddr, info.desc.bInterfaceNumber);
            if (info.desc.bInterfaceClass == TUSB_CLASS_CDC) {      /* ACM: control + data */
                ch32rv_usb_host_mark_claimed(info.daddr, (uint8_t)(info.desc.bInterfaceNumber + 1u));
            }
        }
    }
}

extern "C" void tuh_cdc_umount_cb(uint8_t idx)
{
    if (idx < CFG_TUH_CDC) {
        rx[idx].head = rx[idx].tail = 0;
    }
}

/* ------------------------------------------------------------- host API */
void CH32RVUsbHost::onSerialData(SerialDataCallback cb) { data_cb = cb; }

bool CH32RVUsbHost::sendSerial(const uint8_t *data, size_t length, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(_started ? CH32RVUsbHostError::NotConnected : CH32RVUsbHostError::NotStarted);
    }
    const uint32_t t0 = millis();
    size_t done = 0;
    while (done < length) {
        const uint32_t n = tuh_cdc_write((uint8_t)idx, data + done, (uint32_t)(length - done));
        done += n;
        tuh_cdc_write_flush((uint8_t)idx);
        if (done < length) {
            tuh_task_ext(0, false);
            if (!tuh_cdc_mounted((uint8_t)idx)) {
                return fail(CH32RVUsbHostError::NotConnected);
            }
            if ((uint32_t)(millis() - t0) > 1000u) {
                return fail(CH32RVUsbHostError::Timeout);
            }
        }
    }
    return ok();
}

bool CH32RVUsbHost::sendSerial(const char *text, uint8_t address, uint8_t port)
{
    return sendSerial((const uint8_t *)text, text ? strlen(text) : 0, address, port);
}

bool CH32RVUsbHost::serialReady(uint8_t address, uint8_t port) const
{
    return index_of(address, port) >= 0;
}

uint8_t CH32RVUsbHost::serialPortCount(uint8_t address) const
{
    uint8_t n = 0;
    while (index_of(address, n) >= 0) {
        n++;
    }
    return n;
}

bool CH32RVUsbHost::getSerialPortInfo(CH32RVUsbHostSerialPortInfo &info, uint8_t address, uint8_t port) const
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return false;
    }
    tuh_itf_info_t itf;
    tuh_cdc_itf_get_info((uint8_t)idx, &itf);
    info = CH32RVUsbHostSerialPortInfo();
    info.address = itf.daddr;
    info.port = port == CH32RV_USB_HOST_ANY_PORT ? 0 : port;
    info.vendorSerial = itf.desc.bInterfaceClass == TUSB_CLASS_VENDOR_SPECIFIC;
    info.controlInterfaceNumber = itf.desc.bInterfaceNumber;
    info.dataInterfaceNumber = info.vendorSerial ? itf.desc.bInterfaceNumber : (uint8_t)(itf.desc.bInterfaceNumber + 1u);
    info.ready = true;
    return true;
}

bool CH32RVUsbHost::setSerialBaudRate(uint32_t baud, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    const uint32_t g = arm();
    return settle(*this, tuh_cdc_set_baudrate((uint8_t)idx, baud, wait_done, g));
}

bool CH32RVUsbHost::setSerialConfig(const CH32RVUsbHostSerialConfig &config, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    return set_line_coding(*this, idx, config);
}

bool CH32RVUsbHost::setSerialControlLines(bool dtr, bool rts, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    return set_lines(*this, idx, dtr, rts);
}

/* ------------------------------------------------------------- the Stream */
CH32RVUsbHostCdcSerial::CH32RVUsbHostCdcSerial(CH32RVUsbHost &host) : _host(host) {}

static int bound_index(CH32RVUsbHostCdcSerial &s, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx >= 0 && rx[idx].owner != &s) {
        for (uint8_t i = 0; i < CFG_TUH_CDC; i++) {
            if (rx[i].owner == &s) {
                rx[i].owner = nullptr;
            }
        }
        if (!rx[idx].owner) {
            rx[idx].owner = &s;
            rx[idx].head = rx[idx].tail = 0;
        }
    }
    return idx >= 0 && rx[idx].owner == &s ? idx : -1;
}

bool CH32RVUsbHostCdcSerial::begin(uint32_t baud)
{
    _baud = baud;
    _begun = true;
    const int idx = bound_index(*this, _address, _port);
    if (idx < 0) {
        return true;              /* applied when the port appears */
    }
    CH32RVUsbHostSerialConfig config;
    config.baud = baud;
    return set_line_coding(_host, idx, config) && set_lines(_host, idx, _dtr, _rts);
}

void CH32RVUsbHostCdcSerial::end()
{
    for (uint8_t i = 0; i < CFG_TUH_CDC; i++) {
        if (rx[i].owner == this) {
            rx[i].owner = nullptr;
        }
    }
    _begun = false;
}

bool CH32RVUsbHostCdcSerial::connected() const
{
    return index_of(_address, _port) >= 0;
}

int CH32RVUsbHostCdcSerial::available()
{
    if (!_begun) {
        return 0;
    }
    const int idx = bound_index(*this, _address, _port);
    ch32rv_usb_host_task();
    return idx < 0 ? 0 : rx_count(rx[idx]);
}

int CH32RVUsbHostCdcSerial::read()
{
    const int idx = bound_index(*this, _address, _port);
    if (!_begun || idx < 0) {
        return -1;
    }
    if (!rx_count(rx[idx])) {
        ch32rv_usb_host_task();
    }
    Rx &r = rx[idx];
    if (!rx_count(r)) {
        return -1;
    }
    const uint8_t c = r.buf[r.tail];
    r.tail = (uint16_t)((r.tail + 1u) % sizeof r.buf);
    return c;
}

int CH32RVUsbHostCdcSerial::peek()
{
    const int idx = bound_index(*this, _address, _port);
    if (!_begun || idx < 0 || !rx_count(rx[idx])) {
        return -1;
    }
    return rx[idx].buf[rx[idx].tail];
}

void CH32RVUsbHostCdcSerial::flush()
{
    const int idx = index_of(_address, _port);
    if (idx >= 0) {
        tuh_cdc_write_flush((uint8_t)idx);
    }
}

size_t CH32RVUsbHostCdcSerial::write(uint8_t c)
{
    return write(&c, 1);
}

size_t CH32RVUsbHostCdcSerial::write(const uint8_t *data, size_t length)
{
    if (!_begun) {
        return 0;
    }
    return _host.sendSerial(data, length, _address, _port) ? length : 0;
}

bool CH32RVUsbHostCdcSerial::setBaudRate(uint32_t baud)
{
    _baud = baud;
    return _host.setSerialBaudRate(baud, _address, _port);
}

bool CH32RVUsbHostCdcSerial::setConfig(const CH32RVUsbHostSerialConfig &config)
{
    _baud = config.baud;
    return _host.setSerialConfig(config, _address, _port);
}

bool CH32RVUsbHostCdcSerial::setDtr(bool on)
{
    _dtr = on;
    return _host.setSerialControlLines(_dtr, _rts, _address, _port);
}

bool CH32RVUsbHostCdcSerial::setRts(bool on)
{
    _rts = on;
    return _host.setSerialControlLines(_dtr, _rts, _address, _port);
}

#else /* no host */

void CH32RVUsbHost::onSerialData(SerialDataCallback) {}
bool CH32RVUsbHost::sendSerial(const uint8_t *, size_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::sendSerial(const char *, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::serialReady(uint8_t, uint8_t) const { return false; }
uint8_t CH32RVUsbHost::serialPortCount(uint8_t) const { return 0; }
bool CH32RVUsbHost::getSerialPortInfo(CH32RVUsbHostSerialPortInfo &, uint8_t, uint8_t) const { return false; }
bool CH32RVUsbHost::setSerialBaudRate(uint32_t, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::setSerialConfig(const CH32RVUsbHostSerialConfig &, uint8_t, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::setSerialControlLines(bool, bool, uint8_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }

CH32RVUsbHostCdcSerial::CH32RVUsbHostCdcSerial(CH32RVUsbHost &host) : _host(host) {}
bool CH32RVUsbHostCdcSerial::begin(uint32_t) { return false; }
void CH32RVUsbHostCdcSerial::end() {}
bool CH32RVUsbHostCdcSerial::connected() const { return false; }
int CH32RVUsbHostCdcSerial::available() { return 0; }
int CH32RVUsbHostCdcSerial::read() { return -1; }
int CH32RVUsbHostCdcSerial::peek() { return -1; }
void CH32RVUsbHostCdcSerial::flush() {}
size_t CH32RVUsbHostCdcSerial::write(uint8_t) { return 0; }
size_t CH32RVUsbHostCdcSerial::write(const uint8_t *, size_t) { return 0; }
bool CH32RVUsbHostCdcSerial::setBaudRate(uint32_t) { return false; }
bool CH32RVUsbHostCdcSerial::setConfig(const CH32RVUsbHostSerialConfig &) { return false; }
bool CH32RVUsbHostCdcSerial::setDtr(bool) { return false; }
bool CH32RVUsbHostCdcSerial::setRts(bool) { return false; }

#endif
