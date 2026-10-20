/* CH32RVUsbHost: CDC-ACM serial, and the FTDI / CP210x / CH34x / PL2303 USB
 * serial chips. Ports are numbered per device in the order they were found.
 *
 * The chips' requests, baud rate encodings and start-up sequences follow
 * TinyUSB's cdc_host.c (MIT; Ha Thach, Heiko Kuester and contributors), which
 * took them from the Linux kernel drivers; here they run as blocking control
 * transfers from the task. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

enum Driver : uint8_t { ACM, FTDI, CP210X, CH34X, PL2303 };

/* Which chip a vendor-class interface is, by VID and PID. */
struct VidPid {
    uint16_t vid, pid;
    Driver driver;
};
const VidPid serial_chips[] = {
    {0x0403, 0x6001, FTDI}, {0x0403, 0x6006, FTDI}, {0x0403, 0x6010, FTDI}, {0x0403, 0x6011, FTDI},
    {0x0403, 0x6014, FTDI}, {0x0403, 0x6015, FTDI}, {0x0403, 0x6040, FTDI}, {0x0403, 0x6041, FTDI},
    {0x0403, 0x6042, FTDI}, {0x0403, 0x6043, FTDI}, {0x0403, 0x6044, FTDI}, {0x0403, 0x6045, FTDI},
    {0x0403, 0x6048, FTDI}, {0x0403, 0x8372, FTDI}, {0x0403, 0xFBFA, FTDI}, {0x0403, 0xCD18, FTDI},
    {0x10C4, 0xEA60, CP210X}, {0x10C4, 0xEA61, CP210X}, {0x10C4, 0xEA70, CP210X},
    {0x1A86, 0x5523, CH34X}, {0x1A86, 0x7522, CH34X}, {0x1A86, 0x7523, CH34X}, {0x1A86, 0xE523, CH34X},
    {0x4348, 0x5523, CH34X}, {0x2184, 0x0057, CH34X}, {0x9986, 0x7523, CH34X},
    {0x067B, 0x2303, PL2303}, {0x067B, 0x2304, PL2303}, {0x067B, 0x23A3, PL2303}, {0x067B, 0x23B3, PL2303},
    {0x067B, 0x23C3, PL2303}, {0x067B, 0x23D3, PL2303}, {0x067B, 0x23E3, PL2303}, {0x067B, 0x23F3, PL2303},
};

/* FTDI chips by bcdDevice (the kernel's ftdi_determine_type()). */
enum FtdiType : uint8_t { FTDI_UNKNOWN, FTDI_SIO, FTDI_232B, FTDI_232R_CLASS, FTDI_H_CLASS };

/* PL2303 kinds (pl2303_startup()). */
enum Pl2303Type : uint8_t { PL_H, PL_HX, PL_TA, PL_TB, PL_HXD, PL_HXN, PL_UNKNOWN };
struct Pl2303Data {
    uint32_t maxBaud;
    bool legacy, noDivisors, altDivisors;
};
const Pl2303Data pl2303_data[6] = {
    {1228800, true, false, false},  {6000000, false, false, false}, {6000000, false, false, true},
    {12000000, false, false, true}, {12000000, false, false, false}, {12000000, false, true, false},
};

#define CDC_PORTS 2

struct Port {
    bool claimed, mounted;
    Driver driver;
    uint8_t daddr, itf;                      /* the control (ACM) or the vendor interface */
    uint8_t epIn, epOut, epNotify;
    uint16_t mpsIn, mpsOut;
    uint8_t ftdiType, ftdiChannel;
    uint8_t plType;
    CH32RVUsbHostSerialConfig config;
    bool dtr, rts;
    volatile bool inBusy;
    volatile uint8_t pendingLength;          /* a received packet the task has not handed on */
    uint8_t in[64] __attribute__((aligned(4)));
    uint8_t ctl[8] __attribute__((aligned(4)));
};
Port ports[CDC_PORTS];

/* Received bytes go to onSerialData() and to the CH32RVUsbHostCdcSerial bound
 * to that port. With neither, they stay in the packet buffer (the device is
 * NAKed until someone reads). */
struct Rx {
    CH32RVUsbHostCdcSerial *owner;
    uint8_t buf[CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE];
    uint16_t head, tail;
};
Rx rx[CDC_PORTS];
CH32RVUsbHost::SerialDataCallback data_cb;

uint16_t cdc_open(uint8_t daddr, const uint8_t *itf, uint16_t length);
void cdc_configured(uint8_t daddr, uint8_t itf);
void poll(CH32RVUsbHost &host);
void cdc_detach(uint8_t daddr);
CH32RVUsbHostGroup cdc_group = {cdc_open, cdc_configured, poll, cdc_detach, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(cdc_group); }
} join;

uint16_t rx_count(const Rx &r)
{
    return (uint16_t)((r.head + sizeof r.buf - r.tail) % sizeof r.buf);
}

/* The port index for (address, port); -1 when there is none. */
int index_of(uint8_t address, uint8_t port)
{
    uint8_t seen = 0;
    uint8_t first_dev = 0;
    for (uint8_t i = 0; i < CDC_PORTS; i++) {
        if (!ports[i].mounted) {
            continue;
        }
        const uint8_t d = ports[i].daddr;
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

uint8_t control(Port &p, uint8_t requestType, uint8_t request, uint16_t value, uint16_t index, uint8_t *data,
                uint16_t length)
{
    return ch32rv_usb_host_control_sync(p.daddr, requestType, request, value, index, data, length, nullptr, 1000);
}

/* ------------------------------------------------------------- the chips */
uint32_t div_round(uint32_t a, uint32_t b) { return (a + b / 2) / b; }

uint32_t ftdi_232bm_divisor(uint32_t baud, uint32_t base)
{
    static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    const uint32_t divisor3 = div_round(base, 2 * baud);    /* the divisor shifted 3 bits left */
    uint32_t divisor = divisor3 >> 3;
    divisor |= (uint32_t)divfrac[divisor3 & 7] << 14;
    if (divisor == 1) {                                     /* 1.0 */
        divisor = 0;
    } else if (divisor == 0x4001) {                         /* 1.5 */
        divisor = 1;
    }
    return divisor;
}

uint32_t ftdi_2232h_divisor(uint32_t baud, uint32_t base)
{
    static const uint8_t divfrac[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    const uint32_t divisor3 = div_round(8 * base, 10 * baud);   /* 10-bit sampling at high speed */
    uint32_t divisor = divisor3 >> 3;
    divisor |= (uint32_t)divfrac[divisor3 & 7] << 14;
    if (divisor == 1) {
        divisor = 0;
    } else if (divisor == 0x4001) {
        divisor = 1;
    }
    return divisor | 0x00020000;                            /* no divide by 2.5: up to 12 Mbaud */
}

uint32_t ftdi_divisor(const Port &p, uint32_t baud)
{
    if (!baud) {
        return 0xFFFFFFFFu;
    }
    switch (p.ftdiType) {
    case FTDI_SIO: {
        static const uint32_t rates[10] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
        for (uint32_t i = 0; i < 10; i++) {
            if (rates[i] == baud) {
                return i;
            }
        }
        return 0xFFFFFFFFu;
    }
    case FTDI_232B:
    case FTDI_232R_CLASS:
        return baud <= 3000000 ? ftdi_232bm_divisor(baud, 48000000) : 0xFFFFFFFFu;
    case FTDI_H_CLASS:
        if (baud > 12000000) {
            return 0xFFFFFFFFu;
        }
        return baud >= 1200 ? ftdi_2232h_divisor(baud, 120000000) : ftdi_232bm_divisor(baud, 48000000);
    }
    return 0xFFFFFFFFu;
}

/* CH34x: divisor and prescaler as one register pair; bit 7 of the
 * prescaler keeps the chip from buffering (Linux). */
uint16_t ch34x_divisor_prescaler(uint32_t baud)
{
    uint8_t a, b;
    if (!baud || baud > 2000000) {
        return 0;
    }
    if (baud == 921600) {
        a = 0xF3;
        b = 7;
    } else if (baud == 307200) {
        a = 0xD9;
        b = 7;
    } else {
        uint32_t c;
        if (baud > 6000000 / 255) {
            b = 3;
            c = 6000000;
        } else if (baud > 750000 / 255) {
            b = 2;
            c = 750000;
        } else if (baud > 93750 / 255) {
            b = 1;
            c = 93750;
        } else {
            b = 0;
            c = 11719;
        }
        a = (uint8_t)(c / baud);
        if (a == 0 || a == 0xFF) {
            return 0;
        }
        if ((c / a - baud) > (baud - c / (a + 1))) {
            a++;
        }
        a = (uint8_t)(256 - a);
    }
    return (uint16_t)((uint16_t)a << 8 | 0x80 | b);
}

uint8_t ch34x_lcr(const CH32RVUsbHostSerialConfig &c)
{
    if (c.dataBits < 5 || c.dataBits > 8 || c.stopBits == 1) {   /* no 1.5 stop bits */
        return 0;
    }
    uint8_t lcr = (uint8_t)(0x80 | 0x40 | (c.dataBits - 5));     /* RX, TX enabled */
    switch (c.parity) {
    case 1: lcr |= 0x08; break;                                   /* odd */
    case 2: lcr |= 0x08 | 0x10; break;                            /* even */
    case 3: lcr |= 0x08 | 0x20; break;                            /* mark */
    case 4: lcr |= 0x08 | 0x20 | 0x10; break;                     /* space */
    default: break;
    }
    if (c.stopBits == 2) {
        lcr |= 0x04;
    }
    return lcr;
}

/* PL2303: the rates it takes directly; others by divisor (or, for the
 * newest, directly anyway). */
void pl2303_encode_baud(const Port &p, uint32_t baud, uint8_t *buf)
{
    static const uint32_t direct[] = {75,     150,    300,    600,     1200,    1800,    2400,    3600,   4800,
                                      7200,   9600,   14400,  19200,   28800,   38400,   57600,   115200, 230400,
                                      460800, 614400, 921600, 1228800, 2457600, 3000000, 6000000};
    const Pl2303Data &t = pl2303_data[p.plType < PL_UNKNOWN ? p.plType : PL_HX];
    uint32_t nearest = baud;
    if (!t.noDivisors) {
        uint8_t i = 0;
        const uint8_t n = sizeof direct / sizeof direct[0];
        while (i < n && direct[i] <= baud) {
            i++;
        }
        if (i == n) {
            nearest = direct[n - 1];
        } else if (i > 0 && (direct[i] - baud) > (baud - direct[i - 1])) {
            nearest = direct[i - 1];
        } else {
            nearest = direct[i];
        }
    }
    if (nearest == baud) {
        buf[0] = (uint8_t)baud;
        buf[1] = (uint8_t)(baud >> 8);
        buf[2] = (uint8_t)(baud >> 16);
        buf[3] = (uint8_t)(baud >> 24);
        return;
    }
    const uint32_t baseline = 12000000u * 32u;
    uint32_t mantissa = baseline / baud;
    if (!mantissa) {
        mantissa = 1;
    }
    uint32_t exponent = 0;
    if (t.altDivisors) {           /* baud = 12M * 32 / (mantissa * 2^exponent) */
        while (mantissa >= 2048) {
            if (exponent < 15) {
                mantissa >>= 1;
                exponent++;
            } else {
                mantissa = 2047;
                break;
            }
        }
        buf[3] = 0x80;
        buf[2] = (uint8_t)(exponent & 1);
        buf[1] = (uint8_t)((((exponent & ~1u) << 4) | (mantissa >> 8)) & 0xFF);
        buf[0] = (uint8_t)mantissa;
    } else {                       /* baud = 12M * 32 / (mantissa * 4^exponent) */
        while (mantissa >= 512) {
            if (exponent < 7) {
                mantissa >>= 2;
                exponent++;
            } else {
                mantissa = 511;
                break;
            }
        }
        buf[3] = 0x80;
        buf[2] = 0;
        buf[1] = (uint8_t)((exponent << 1 | mantissa >> 8) & 0xFF);
        buf[0] = (uint8_t)mantissa;
    }
}

Pl2303Type pl2303_type(const uint8_t *dev, bool hxStatus)
{
    const uint16_t usb = (uint16_t)(dev[2] | dev[3] << 8), version = (uint16_t)(dev[12] | dev[13] << 8);
    if (dev[4] == 0x02 || dev[7] != 0x40) {
        return PL_H;               /* legacy PL2303H, variants 0 and 1 */
    }
    if (usb == 0x101 || usb == 0x110) {
        return version == 0x400 ? PL_HXD : PL_HX;
    }
    if (usb == 0x200) {
        switch (version) {
        case 0x100: case 0x105: case 0x305: case 0x400: case 0x405:
        case 0x505: case 0x600: case 0x605: case 0x700: case 0x705:
            return PL_HXN;
        case 0x300: return hxStatus ? PL_TA : PL_HXN;     /* GT / TA */
        case 0x500: return hxStatus ? PL_TB : PL_HXN;     /* GE / TB */
        }
    }
    return PL_UNKNOWN;
}

bool pl2303_vendor_read(Port &p, uint16_t value)
{
    return control(p, 0xC0, p.plType == PL_HXN ? 0x81 : 0x01, value, 0, p.ctl, 1) == CH32RV_USBH_OK;
}

bool pl2303_vendor_write(Port &p, uint16_t value, uint16_t index)
{
    return control(p, 0x40, p.plType == PL_HXN ? 0x80 : 0x01, value, index, nullptr, 0) == CH32RV_USBH_OK;
}

/* ------------------------------------------------------------- line settings */
bool apply_coding(Port &p, const CH32RVUsbHostSerialConfig &c)
{
    switch (p.driver) {
    case ACM: {
        uint8_t *b = p.ctl;
        b[0] = (uint8_t)c.baud;
        b[1] = (uint8_t)(c.baud >> 8);
        b[2] = (uint8_t)(c.baud >> 16);
        b[3] = (uint8_t)(c.baud >> 24);
        b[4] = c.stopBits;
        b[5] = c.parity;
        b[6] = c.dataBits;
        return control(p, 0x21, 0x20, 0, p.itf, b, 7) == CH32RV_USBH_OK;   /* SET_LINE_CODING */
    }
    case FTDI: {
        const uint32_t div = ftdi_divisor(p, c.baud);
        if (div == 0xFFFFFFFFu || c.dataBits < 7 || c.dataBits > 8) {
            return false;
        }
        uint16_t index = (uint16_t)(div >> 16);
        if (p.ftdiChannel) {
            index = (uint16_t)((index << 8) | p.ftdiChannel);
        }
        if (control(p, 0x40, 3, (uint16_t)div, index, nullptr, 0) != CH32RV_USBH_OK) {   /* SET_BAUDRATE */
            return false;
        }
        const uint16_t data = (uint16_t)((c.dataBits & 0xF) | (c.parity & 7) << 8 | (c.stopBits & 3) << 11);
        return control(p, 0x40, 4, data, p.ftdiChannel, nullptr, 0) == CH32RV_USBH_OK;     /* SET_DATA */
    }
    case CP210X: {
        if (c.dataBits < 5 || c.dataBits > 8) {
            return false;
        }
        uint8_t *b = p.ctl;
        b[0] = (uint8_t)c.baud;
        b[1] = (uint8_t)(c.baud >> 8);
        b[2] = (uint8_t)(c.baud >> 16);
        b[3] = (uint8_t)(c.baud >> 24);
        if (control(p, 0x41, 0x1E, 0, p.itf, b, 4) != CH32RV_USBH_OK) {                    /* SET_BAUDRATE */
            return false;
        }
        const uint16_t lcr = (uint16_t)((c.dataBits & 0xF) << 8 | (c.parity & 0xF) << 4 | (c.stopBits & 0xF));
        return control(p, 0x41, 0x03, lcr, p.itf, nullptr, 0) == CH32RV_USBH_OK;           /* SET_LINE_CTL */
    }
    case CH34X: {
        const uint16_t div = ch34x_divisor_prescaler(c.baud);
        const uint8_t lcr = ch34x_lcr(c);
        if (!div || !lcr) {
            return false;
        }
        return control(p, 0x40, 0x9A, 0x1312, div, nullptr, 0) == CH32RV_USBH_OK &&          /* divisor, prescaler */
               control(p, 0x40, 0x9A, 0x2518, lcr, nullptr, 0) == CH32RV_USBH_OK;            /* LCR2, LCR */
    }
    case PL2303: {
        const Pl2303Data &t = pl2303_data[p.plType < PL_UNKNOWN ? p.plType : PL_HX];
        if (!c.baud || c.baud > t.maxBaud || c.dataBits < 5 || c.dataBits > 8) {
            return false;
        }
        uint8_t *b = p.ctl;
        pl2303_encode_baud(p, c.baud, b);
        b[4] = c.stopBits;
        b[5] = c.parity;
        b[6] = c.dataBits;
        return control(p, 0x21, 0x20, 0, 0, b, 7) == CH32RV_USBH_OK;
    }
    }
    return false;
}

bool apply_lines(Port &p, bool dtr, bool rts)
{
    switch (p.driver) {
    case ACM:
        return control(p, 0x21, 0x22, (uint16_t)((dtr ? 1 : 0) | (rts ? 2 : 0)), p.itf, nullptr, 0) == CH32RV_USBH_OK;
    case FTDI:
        return control(p, 0x40, 1, (uint16_t)(0x0100 | (dtr ? 1 : 0) | 0x0200 | (rts ? 2 : 0)), p.ftdiChannel,
                       nullptr, 0) == CH32RV_USBH_OK;
    case CP210X:
        return control(p, 0x41, 0x07, (uint16_t)(0x0300 | (dtr ? 1 : 0) | (rts ? 2 : 0)), p.itf, nullptr, 0) ==
               CH32RV_USBH_OK;
    case CH34X:                    /* the signals are inverted */
        return control(p, 0x40, 0xA4, (uint8_t)~((rts ? 0x40 : 0) | (dtr ? 0x20 : 0)), 0, nullptr, 0) ==
               CH32RV_USBH_OK;
    case PL2303:
        return control(p, 0x21, 0x22, (uint16_t)((dtr ? 1 : 0) | (rts ? 2 : 0)), 0, nullptr, 0) == CH32RV_USBH_OK;
    }
    return false;
}

/* The chip's own start-up, before the line settings (each after the kernel
 * driver's probe and open). */
bool chip_init(Port &p)
{
    const uint8_t *dev = ch32rv_usb_host_device_descriptor(p.daddr);
    if (!dev) {
        return false;
    }
    switch (p.driver) {
    case ACM:
        return true;
    case FTDI: {
        const uint16_t version = (uint16_t)(dev[12] | dev[13] << 8);
        p.ftdiChannel = (uint8_t)(1 + p.itf);                    /* channel A, B, ... on the multi-port parts */
        switch (version) {
        case 0x400: p.ftdiType = FTDI_232B; p.ftdiChannel = 0; break;
        case 0x500: p.ftdiType = FTDI_232R_CLASS; break;          /* FT2232C/D */
        case 0x600: p.ftdiType = FTDI_232R_CLASS; p.ftdiChannel = 0; break;
        case 0x1000: p.ftdiType = FTDI_232R_CLASS; break;         /* FT-X */
        case 0x700: case 0x800: case 0x900: case 0x2800: case 0x2900: case 0x3000:
        case 0x3100: case 0x3200: case 0x3300: case 0x3600:
            p.ftdiType = FTDI_H_CLASS;
            break;
        default:
            if (version < 0x200) {
                p.ftdiType = FTDI_SIO;
                p.ftdiChannel = 0;
            } else {
                return false;                                     /* FT232A and unknown ones */
            }
            break;
        }
        return control(p, 0x40, 0, 0, p.ftdiChannel, nullptr, 0) == CH32RV_USBH_OK &&   /* SIO reset */
               control(p, 0x40, 2, 0, p.ftdiChannel, nullptr, 0) == CH32RV_USBH_OK;     /* no flow control */
    }
    case CP210X:
        return control(p, 0x41, 0x00, 1, p.itf, nullptr, 0) == CH32RV_USBH_OK;          /* IFC_ENABLE */
    case CH34X: {
        if (control(p, 0xC0, 0x5F, 0, 0, p.ctl, 2) != CH32RV_USBH_OK) {                  /* READ_VERSION */
            return false;
        }
        if (p.ctl[0] >= 0x30) {    /* earlier versions are programmed otherwise */
            const uint16_t div = ch34x_divisor_prescaler(p.config.baud);
            const uint8_t lcr = ch34x_lcr(p.config);
            if (!div || !lcr ||
                control(p, 0x40, 0xA1, (uint16_t)(lcr << 8 | 0x9C), div, nullptr, 0) != CH32RV_USBH_OK) {   /* SERIAL_INIT */
                return false;
            }
        }
        return control(p, 0x40, 0x9A, 0x0F2C, 0x0007, nullptr, 0) == CH32RV_USBH_OK &&   /* as WCH's driver does */
               control(p, 0x40, 0x9A, 0x2727, 0x0000, nullptr, 0) == CH32RV_USBH_OK;     /* no hardware flow control */
    }
    case PL2303: {
        Pl2303Type t = pl2303_type(dev, false);
        const uint16_t version = (uint16_t)(dev[12] | dev[13] << 8);
        if ((uint16_t)(dev[2] | dev[3] << 8) == 0x200 && (version == 0x300 || version == 0x500) && dev[4] != 0x02 &&
            dev[7] == 0x40) {
            /* TA / TB answer the HX status read; the others STALL it. */
            const bool hx = control(p, 0xC0, 0x01, 0x8080, 0, p.ctl, 1) == CH32RV_USBH_OK;
            t = pl2303_type(dev, hx);
        }
        if (t == PL_UNKNOWN) {
            return false;
        }
        p.plType = t;
        if (t != PL_HXN) {         /* the kernel's sequence, purpose unknown */
            if (!(pl2303_vendor_read(p, 0x8484) && pl2303_vendor_write(p, 0x0404, 0) && pl2303_vendor_read(p, 0x8484) &&
                  pl2303_vendor_read(p, 0x8383) && pl2303_vendor_read(p, 0x8484) && pl2303_vendor_write(p, 0x0404, 1) &&
                  pl2303_vendor_read(p, 0x8484) && pl2303_vendor_read(p, 0x8383) && pl2303_vendor_write(p, 0, 1) &&
                  pl2303_vendor_write(p, 1, 0) && pl2303_vendor_write(p, 2, pl2303_data[t].legacy ? 0x24 : 0x44))) {
                return false;
            }
        }
        if (pl2303_data[t].legacy) {
            return ch32rv_usb_host_clear_halt(p.daddr, p.epOut) && ch32rv_usb_host_clear_halt(p.daddr, p.epIn);
        }
        if (t == PL_HXN) {
            return pl2303_vendor_write(p, 0x07, 0x03);              /* reset both data pipes */
        }
        return pl2303_vendor_write(p, 8, 0) && pl2303_vendor_write(p, 9, 0);
    }
    }
    return false;
}

/* ------------------------------------------------------------- the group */
void in_done(void *user, uint8_t result, uint16_t length);

void arm_in(Port &p)
{
    if (p.mounted && !p.inBusy && !p.pendingLength) {
        p.inBusy = ch32rv_usb_host_xfer(p.daddr, p.epIn, p.in, p.mpsIn, in_done, &p);
    }
}

void in_done(void *user, uint8_t result, uint16_t length)
{
    Port &p = *(Port *)user;
    p.inBusy = false;
    if (!p.mounted || result != CH32RV_USBH_OK) {
        if (p.mounted && result == CH32RV_USBH_STALLED) {
            return;                /* left alone until the device is reset */
        }
        arm_in(p);
        return;
    }
    if (p.driver == FTDI) {        /* two modem status bytes in front of every packet */
        if (length <= 2) {
            arm_in(p);
            return;
        }
        memmove(p.in, p.in + 2, (size_t)(length - 2));
        length = (uint16_t)(length - 2);
    }
    if (!length) {
        arm_in(p);
        return;
    }
    p.pendingLength = (uint8_t)length;   /* the task hands it on, then the endpoint is armed again */
}

/* The interfaces taken: CDC-ACM (control, and the data interface after it),
 * or a vendor interface of a known USB serial chip. */
uint16_t cdc_open(uint8_t daddr, const uint8_t *itf, uint16_t length)
{
    Port *p = nullptr;
    for (Port &x : ports) {
        if (!x.claimed) {
            p = &x;
            break;
        }
    }
    if (!p) {
        return 0;
    }
    Port n = {};
    n.daddr = daddr;
    n.itf = itf[2];
    const uint8_t *eps[3] = {};
    uint8_t neps = 0;
    uint16_t at = itf[0];
    if (itf[5] == 0x02 && itf[6] == 0x02) {   /* ACM */
        n.driver = ACM;
        while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4) {
            if (itf[at + 1] == 5 && neps < 3) {
                eps[neps++] = itf + at;
            }
            at = (uint16_t)(at + itf[at]);
        }
        if (at + 9 <= length && itf[at + 1] == 4 && itf[at + 5] == 0x0A) {   /* the data interface */
            at = (uint16_t)(at + itf[at]);
            while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4 && itf[at + 1] != 11) {
                if (itf[at + 1] == 5 && neps < 3) {
                    eps[neps++] = itf + at;
                }
                at = (uint16_t)(at + itf[at]);
            }
        }
    } else if (itf[5] == 0xFF) {
        const uint8_t *dev = ch32rv_usb_host_device_descriptor(daddr);
        if (!dev) {
            return 0;
        }
        const uint16_t vid = (uint16_t)(dev[8] | dev[9] << 8), pid = (uint16_t)(dev[10] | dev[11] << 8);
        bool known = false;
        for (const VidPid &c : serial_chips) {
            if (c.vid == vid && c.pid == pid) {
                n.driver = c.driver;
                known = true;
                break;
            }
        }
        if (!known) {
            return 0;
        }
        while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4) {
            if (itf[at + 1] == 5 && neps < 3) {
                eps[neps++] = itf + at;
            }
            at = (uint16_t)(at + itf[at]);
        }
    } else {
        return 0;
    }
    for (uint8_t i = 0; i < neps; i++) {
        const uint8_t *e = eps[i];
        const uint8_t type = e[3] & 3;
        const uint16_t mps = (uint16_t)((e[4] | e[5] << 8) & 0x7FF);
        if (type == CH32RV_USBH_BULK && (e[2] & 0x80) && !n.epIn) {
            n.epIn = e[2];
            n.mpsIn = mps < sizeof n.in ? mps : sizeof n.in;
        } else if (type == CH32RV_USBH_BULK && !(e[2] & 0x80) && !n.epOut) {
            n.epOut = e[2];
            n.mpsOut = mps;
        } else if (type == CH32RV_USBH_INTERRUPT && (e[2] & 0x80)) {
            n.epNotify = e[2];
        } else {
            continue;
        }
        if (type == CH32RV_USBH_BULK && !ch32rv_usb_host_ep_open(daddr, e)) {
            return 0;
        }
    }
    if (!n.epIn || !n.epOut || !n.mpsIn) {
        return 0;
    }
    n.claimed = true;
    *p = n;
    return at;
}

/* Start the chip, then 115200 8N1 with DTR and RTS on - what a terminal does
 * when it opens the port. */
void cdc_configured(uint8_t daddr, uint8_t itf)
{
    for (uint8_t i = 0; i < CDC_PORTS; i++) {
        Port &p = ports[i];
        if (!p.claimed || p.mounted || p.daddr != daddr || p.itf != itf) {
            continue;
        }
        p.config = CH32RVUsbHostSerialConfig();
        p.dtr = p.rts = true;
        if (!chip_init(p) || !ch32rv_usb_host_connected(daddr)) {
            return;
        }
        if (!(p.driver == CH34X && p.ctl[0] >= 0x30)) {   /* the CH34x's serial init set it already */
            apply_coding(p, p.config);
        }
        apply_lines(p, p.dtr, p.rts);
        rx[i].head = rx[i].tail = 0;
        p.mounted = true;
        ch32rv_usb_host_mark_claimed(daddr, p.itf);
        if (p.driver == ACM) {
            ch32rv_usb_host_mark_claimed(daddr, (uint8_t)(p.itf + 1u));   /* the data interface */
        }
        arm_in(p);
    }
}

void cdc_detach(uint8_t daddr)
{
    for (uint8_t i = 0; i < CDC_PORTS; i++) {
        if (ports[i].claimed && ports[i].daddr == daddr) {
            memset(&ports[i], 0, sizeof ports[i]);
            rx[i].head = rx[i].tail = 0;
        }
    }
}

void poll(CH32RVUsbHost &host)
{
    (void)host;
    for (uint8_t i = 0; i < CDC_PORTS; i++) {
        Port &p = ports[i];
        Rx &r = rx[i];
        const uint8_t n = p.pendingLength;
        if (!p.mounted || !n) {
            continue;
        }
        if (!data_cb && !r.owner) {
            continue;              /* nobody listens: the device waits */
        }
        if (r.owner && (uint16_t)(sizeof r.buf - 1u - rx_count(r)) < n) {
            continue;              /* the Stream is full: wait for its reader */
        }
        if (r.owner) {
            for (uint8_t k = 0; k < n; k++) {
                r.buf[r.head] = p.in[k];
                r.head = (uint16_t)((r.head + 1u) % sizeof r.buf);
            }
        }
        if (data_cb) {
            CH32RVUsbHostSerialData d;
            d.address = p.daddr;
            d.interfaceNumber = p.itf;
            uint8_t port = 0;
            for (uint8_t j = 0; j < i; j++) {
                port = (uint8_t)(port + (ports[j].mounted && ports[j].daddr == p.daddr));
            }
            d.port = port;
            d.data = p.in;
            d.length = n;
            data_cb(d);
        }
        p.pendingLength = 0;
        arm_in(p);
    }
}

}  // namespace

/* ------------------------------------------------------------- host API */
void CH32RVUsbHost::onSerialData(SerialDataCallback cb) { data_cb = cb; }

bool CH32RVUsbHost::sendSerial(const uint8_t *data, size_t length, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(_started ? CH32RVUsbHostError::NotConnected : CH32RVUsbHostError::NotStarted);
    }
    if (length && !data) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    Port &p = ports[idx];
    size_t done = 0;
    while (done < length) {
        const size_t n = length - done < 1024u ? length - done : 1024u;
        const uint8_t r = ch32rv_usb_host_xfer_sync(p.daddr, p.epOut, const_cast<uint8_t *>(data + done), (uint16_t)n,
                                                    nullptr, 1000);
        if (r != CH32RV_USBH_OK) {
            return fail(p.mounted ? ch32rv_usb_host_result_error(r) : CH32RVUsbHostError::NotConnected);
        }
        done += n;
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
    const Port &p = ports[idx];
    info = CH32RVUsbHostSerialPortInfo();
    info.address = p.daddr;
    info.port = port == CH32RV_USB_HOST_ANY_PORT ? 0 : port;
    info.vendorSerial = p.driver != ACM;
    info.controlInterfaceNumber = p.itf;
    info.dataInterfaceNumber = info.vendorSerial ? p.itf : (uint8_t)(p.itf + 1u);
    info.ready = true;
    return true;
}

bool CH32RVUsbHost::setSerialBaudRate(uint32_t baud, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    CH32RVUsbHostSerialConfig c = ports[idx].config;
    c.baud = baud;
    if (!apply_coding(ports[idx], c)) {
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    ports[idx].config = c;
    return ok();
}

bool CH32RVUsbHost::setSerialConfig(const CH32RVUsbHostSerialConfig &config, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (!apply_coding(ports[idx], config)) {
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    ports[idx].config = config;
    return ok();
}

bool CH32RVUsbHost::setSerialControlLines(bool dtr, bool rts, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (!apply_lines(ports[idx], dtr, rts)) {
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    ports[idx].dtr = dtr;
    ports[idx].rts = rts;
    return ok();
}

/* ------------------------------------------------------------- the Stream */
CH32RVUsbHostCdcSerial::CH32RVUsbHostCdcSerial(CH32RVUsbHost &host) : _host(host) {}

static int bound_index(CH32RVUsbHostCdcSerial &s, uint8_t address, uint8_t port)
{
    const int idx = index_of(address, port);
    if (idx >= 0 && rx[idx].owner != &s) {
        for (uint8_t i = 0; i < CDC_PORTS; i++) {
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
    return _host.setSerialConfig(config, ports[idx].daddr, (uint8_t)_port) &&
           _host.setSerialControlLines(_dtr, _rts, ports[idx].daddr, (uint8_t)_port);
}

void CH32RVUsbHostCdcSerial::end()
{
    for (uint8_t i = 0; i < CDC_PORTS; i++) {
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
    /* write() returns once the data has gone. */
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
