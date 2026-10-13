/* The chip side of TinyUSB: clocks and PHY, the interrupt, the task hook, the
 * descriptors, and the reset into the factory USB bootloader. */
#include <Arduino.h>
#include "ch32rv_registers.h"
#include "tusb.h"
#include "TinyUSB.h"

/* ---------------------------------------------------------- identity
 * pid.codes test VID:PID - fine on a bench, not for a product. A product board
 * or a sketch sets its own with -DCH32RV_USB_VID= / -DCH32RV_USB_PID=. */
#ifndef CH32RV_USB_VID
#define CH32RV_USB_VID 0x1209
#endif
#ifndef CH32RV_USB_PID
#define CH32RV_USB_PID 0x0001
#endif
#ifndef CH32RV_USB_MANUFACTURER
#define CH32RV_USB_MANUFACTURER "ch32-riscv-ug"
#endif
#ifndef CH32RV_USB_PRODUCT
#define CH32RV_USB_PRODUCT "CH32RV Arduino"
#endif

/* ---------------------------------------------------------- X033 / X035 PHY
 * AFIO_CTLR: USB_IOEN hands PC16/PC17 to the USB PHY, USB_PHY_V33 selects the
 * 3.3 V PHY supply (VDD = 3.3 V on every board this core targets so far), and
 * UDP_PUE = 11b puts the 1.5 kOhm full-speed pull-up on D+. Values from the
 * CH32X035 EVT (USBFS_Device_Init). */
#define X03X_AFIO_UDM_PUE_MASK 0x00000003u
#define X03X_AFIO_UDP_PUE_MASK 0x0000000Cu
#define X03X_AFIO_UDP_PUE_1K5  0x0000000Cu
#define X03X_AFIO_USB_PHY_V33  0x00000040u
#define X03X_AFIO_USB_IOEN     0x00000080u

/* FLASH_STATR.BOOT_MODE: after the next software reset, run the BOOT area
 * (the factory bootloader). Unlocked by the flash keys, then the boot-mode
 * keys - the sequence ch32rv's `isp enter --via probe` uses (measured on
 * CH32X035F8U6, ch32rv docs/protocol/factory-isp.ja.md). */
#define X03X_FLASH_KEYR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define X03X_FLASH_STATR         CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define X03X_FLASH_CTLR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define X03X_FLASH_BOOT_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x28u)
#define X03X_FLASH_KEY1          0x45670123u
#define X03X_FLASH_KEY2          0xCDEF89ABu
#define X03X_FLASH_STATR_BOOT_MODE (1u << 14)
#define X03X_FLASH_CTLR_LOCK     (1u << 7)

static void ch32rv_usb_phy_on(void)
{
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    /* PC16 (D-) floating input, PC17 (D+) input with pull-up, as the EVT does;
     * the PHY takes the pads once USB_IOEN is set. */
    pinMode(PC16, INPUT);
    pinMode(PC17, INPUT_PULLUP);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_UDP_PUE_1K5 | X03X_AFIO_USB_IOEN;
}

static void ch32rv_usb_phy_off(void)
{
    CH32RV_AFIO_CTLR &= ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK | X03X_AFIO_USB_IOEN);
}

/* ---------------------------------------------------------- stack lifecycle */
static bool ch32rv_usb_started;

extern "C" void ch32rv_tusb_start(void)
{
    if (ch32rv_usb_started) {
        return;
    }
    ch32rv_usb_phy_on();
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_DEVICE;
    init.speed = TUSB_SPEED_FULL;
    tusb_init(0, &init);
    ch32rv_usb_started = true;
}

/* main() and delay() call this (Arduino.h). A CDC callback that calls delay()
 * would re-enter tud_task(); the guard makes that a no-op instead. */
extern "C" void ch32rv_usb_task(void)
{
    static volatile bool busy;
    if (!ch32rv_usb_started || busy) {
        return;
    }
    busy = true;
    tud_task();
    busy = false;
}

extern "C" __attribute__((interrupt)) void USBFS_IRQHandler(void)
{
    tud_int_handler(0);
}

extern "C" uint32_t tusb_time_millis_api(void)
{
    return millis();
}

/* ---------------------------------------------------------- bootloader */
[[noreturn]] void TinyUSBDevice::enterBootloader()
{
    /* Leave the bus first so the host sees the port go away now, not at the
     * next transfer timeout; the bootloader then enumerates as 1a86:55e0. */
    if (ch32rv_usb_started) {
        tud_disconnect();
    }
    ch32rv_usb_phy_off();
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {
    }

    noInterrupts();
    X03X_FLASH_KEYR = X03X_FLASH_KEY1;
    X03X_FLASH_KEYR = X03X_FLASH_KEY2;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY1;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY2;
    X03X_FLASH_STATR |= X03X_FLASH_STATR_BOOT_MODE;
    X03X_FLASH_CTLR |= X03X_FLASH_CTLR_LOCK;
    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST;
    for (;;) {
    }
}

bool TinyUSBDevice::mounted()
{
    return tud_mounted();
}

bool TinyUSBDevice::suspended()
{
    return tud_suspended();
}

/* ---------------------------------------------------------- descriptors */
enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT   0x02
#define EPNUM_CDC_IN    0x82

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    /* IAD: the CDC function is two interfaces bound together. */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = CH32RV_USB_VID,
    .idProduct = CH32RV_USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

extern "C" const uint8_t *tud_descriptor_device_cb(void)
{
    return (const uint8_t *)&desc_device;
}

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

/* String descriptors: UTF-16LE built on request. Index 3 is the chip's 64-bit
 * unique ID in hex, so two boards on one host get different serial numbers. */
static uint16_t desc_str[1 + 32];

static uint8_t ascii_to_desc(const char *s)
{
    uint8_t n = 0;
    while (s[n] && n < 32) {
        desc_str[1 + n] = (uint8_t)s[n];
        n++;
    }
    return n;
}

extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t n;
    switch (index) {
    case 0:
        desc_str[1] = 0x0409;
        n = 1;
        break;
    case 1:
        n = ascii_to_desc(CH32RV_USB_MANUFACTURER);
        break;
    case 2:
        n = ascii_to_desc(CH32RV_USB_PRODUCT);
        break;
    case 3: {
        static const char hex[] = "0123456789ABCDEF";
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        const uint64_t uid = ((uint64_t)hi << 32) | lo;
        for (n = 0; n < 16; n++) {
            desc_str[1 + n] = (uint8_t)hex[(uid >> (60 - 4 * n)) & 0xF];
        }
        break;
    }
    case 4:
        n = ascii_to_desc("CDC");
        break;
    default:
        return NULL;
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}
