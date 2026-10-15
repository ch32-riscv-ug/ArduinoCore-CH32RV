/* What differs between the series' USB device controllers: the clock and PHY
 * bring-up, which controller a CH32RVUsbDeviceConfig::controller means, its
 * speed and endpoint count, and its interrupt handlers. Included once, by
 * CH32RVUsbDevice.cpp, where CH32RV_TINYUSB_DEVICE is 1.
 *
 * Every port provides:
 *   port_open(controller)  clocks and PHY on for that controller; None, or
 *                          PortUnavailable when this part cannot do it
 *   port_close()           the reverse, after the stack let go
 *   port_high_speed()      the opened controller runs at high speed
 *   port_last_endpoint()   its highest endpoint number
 * and CH32RV_USB_FACTORY_ISP_ENTRY when enterBootloader() can reach the
 * factory USB bootloader (else it is a plain reset). */
#pragma once

#include "ch32rv_registers.h"
#include "ch32rv_usbfs_irq.h"

/* RCC_CFGR0 USBPRE: the USB 48 MHz is PLLCLK divided by it. Written while
 * the USB clock gate is still off. */
#define CH32RV_USB_USBPRE_SHIFT 22u

static bool port_hs;

static bool port_high_speed(void)
{
    return port_hs;
}

/* ======================================================= CH32X035 / X033 */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)

/* AFIO_CTLR: USB_IOEN hands PC16/PC17 to the USB PHY, USB_PHY_V33 selects the
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
#define CH32RV_USB_FACTORY_ISP_ENTRY 1
#define X03X_FLASH_KEYR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define X03X_FLASH_STATR         CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define X03X_FLASH_CTLR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define X03X_FLASH_BOOT_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x28u)
#define X03X_FLASH_KEY1          0x45670123u
#define X03X_FLASH_KEY2          0xCDEF89ABu
#define X03X_FLASH_STATR_BOOT_MODE (1u << 14)
#define X03X_FLASH_CTLR_LOCK     (1u << 7)

static void port_set_boot_mode(void)
{
    X03X_FLASH_KEYR = X03X_FLASH_KEY1;
    X03X_FLASH_KEYR = X03X_FLASH_KEY2;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY1;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY2;
    X03X_FLASH_STATR |= X03X_FLASH_STATR_BOOT_MODE;
    X03X_FLASH_CTLR |= X03X_FLASH_CTLR_LOCK;
}

static CH32RVUsbDeviceError port_open(CH32RVUsbController controller)
{
    if (controller != CH32RVUsbController::Auto && controller != CH32RVUsbController::FullSpeed) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    /* PC16 (D-) floating input, PC17 (D+) input with pull-up, as the EVT does;
     * the PHY takes the pads once USB_IOEN is set. */
    pinMode(PC16, INPUT);
    pinMode(PC17, INPUT_PULLUP);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_UDP_PUE_1K5 | X03X_AFIO_USB_IOEN;
    return CH32RVUsbDeviceError::None;
}

static void port_close(void)
{
    CH32RV_AFIO_CTLR &= ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK | X03X_AFIO_USB_IOEN);
}

static uint8_t port_last_endpoint(void)
{
    return 7;
}

/* The USBFS vector is shared with CH32RVUsbHost (ch32rv_usbfs_irq.c): the role
 * that starts claims it. */
#define CH32RV_USB_SHARED_IRQ 1
static void port_irq(void)
{
    tud_int_handler(0);
}

/* ======================================================= CH32V103
 * USBFS (the older IP) on PA11 (D-) / PA12 (D+). RCC_CFGR0 bit 22 USBPRE:
 * 0 = PLL / 1.5, 1 = PLL / 1. EXTEN_CTR.USBFS_IO_EN gives the pads to the
 * PHY; USB_5V_SEL stays clear for a 3.3 V supply. EVT
 * ch32v10x_usbfs_device.c USBFS_RCC_Init / USBFS_Device_Init. */
#elif defined(CH32RV_SERIES_CH32V103)

#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 72000000
#define V10X_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define V10X_USBPRE 1u
#endif
#define V10X_EXTEN_CTR         CH32RV_REG32(0x40023800u)
#define V10X_EXTEN_USBFS_IO_EN 0x00000004u
#define V10X_EXTEN_USB_5V_SEL  0x00000008u

static CH32RVUsbDeviceError port_open(CH32RVUsbController controller)
{
#ifdef V10X_USBPRE
    if (controller != CH32RVUsbController::Auto && controller != CH32RVUsbController::FullSpeed) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(1u << CH32RV_USB_USBPRE_SHIFT)) |
                       (V10X_USBPRE << CH32RV_USB_USBPRE_SHIFT);
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
    V10X_EXTEN_CTR = (V10X_EXTEN_CTR & ~V10X_EXTEN_USB_5V_SEL) | V10X_EXTEN_USBFS_IO_EN;
    ch32rv_clock_enable(USBFS);
    return CH32RVUsbDeviceError::None;
#else
    (void)controller;
    return CH32RVUsbDeviceError::PortUnavailable;   /* no 48 MHz from this clock */
#endif
}

static void port_close(void)
{
    ch32rv_clock_disable(USBFS);
    V10X_EXTEN_CTR &= ~V10X_EXTEN_USBFS_IO_EN;
}

static uint8_t port_last_endpoint(void)
{
    return 7;
}

extern "C" __attribute__((interrupt)) void USBFS_IRQHandler(void)
{
    tud_int_handler(0);
}

/* ======================================================= CH32V203 / V208
 * USBFS (OTG_FS, PB6 D- / PB7 D+) and USBD (ST fsdev compatible, PA11 D- /
 * PA12 D+, its 512-byte packet memory shared with CAN1). Both drivers are
 * linked; ch32rv_dcd_dual.c forwards to the chosen one, always as rhport 0.
 * Auto takes USBFS, or USBD on the parts that have only USBD. The pads are
 * taken by the PHY once enabled; they only have to be left as inputs.
 * RCC_CFGR0[23:22] USBPRE: 00 = /1, 01 = /2, 10 = /3. */
#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)

#include "ch32rv_dcd_dual.h"

#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define V2X_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define V2X_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define V2X_USBPRE 2u
#endif
#if defined(CH32RV_PART_CH32V203F6P6) || defined(CH32RV_PART_CH32V203G6U6)
#define V20X_NO_USBFS 1      /* USBD only */
#endif
#if defined(CH32RV_PART_CH32V203F8P6) || defined(CH32RV_PART_CH32V203F8U6)
#define V20X_NO_USBD 1       /* USBFS only */
#endif
#define V20X_EXTEN_CTR       CH32RV_REG32(0x40023800u)
#define V20X_EXTEN_USBD_PU   0x00000002u

static uint8_t v20x_port;

static CH32RVUsbDeviceError port_open(CH32RVUsbController controller)
{
#ifdef V2X_USBPRE
    bool usbd;
    if (controller == CH32RVUsbController::Usbd) {
        usbd = true;
    } else if (controller == CH32RVUsbController::FullSpeed) {
        usbd = false;
    } else if (controller == CH32RVUsbController::Auto) {
#ifdef V20X_NO_USBFS
        usbd = true;
#else
        usbd = false;
#endif
    } else {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
#ifdef V20X_NO_USBFS
    if (!usbd) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
#endif
#ifdef V20X_NO_USBD
    if (usbd) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
#endif
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << CH32RV_USB_USBPRE_SHIFT)) |
                       (V2X_USBPRE << CH32RV_USB_USBPRE_SHIFT);
    if (usbd) {
        pinMode(PA11, INPUT);
        pinMode(PA12, INPUT);
        ch32rv_clock_enable(USB);
        ch32rv_dcd_select(CH32RV_DCD_USBD);
    } else {
        pinMode(PB6, INPUT);
        pinMode(PB7, INPUT);
        ch32rv_clock_enable(USBFS);
        ch32rv_dcd_select(CH32RV_DCD_USBFS);
    }
    v20x_port = usbd ? CH32RV_DCD_USBD : CH32RV_DCD_USBFS;
    return CH32RVUsbDeviceError::None;
#else
    (void)controller;
    return CH32RVUsbDeviceError::PortUnavailable;   /* no 48 MHz from this clock */
#endif
}

static void port_close(void)
{
    if (v20x_port == CH32RV_DCD_USBD) {
        V20X_EXTEN_CTR &= ~V20X_EXTEN_USBD_PU;
        ch32rv_clock_disable(USB);
    } else {
        ch32rv_clock_disable(USBFS);
    }
}

static uint8_t port_last_endpoint(void)
{
    return 7;
}

/* Both report as rhport 0 (ch32rv_dcd_dual.c). USBD's two interrupts are
 * shared with CAN1. */
/* The USBFS vector is shared with CH32RVUsbHost (ch32rv_usbfs_irq.c). */
#define CH32RV_USB_SHARED_IRQ 1
static void port_irq(void)
{
    tud_int_handler(0);
}

extern "C" __attribute__((interrupt)) void USB_HP_CAN1_TX_IRQHandler(void)
{
    tud_int_handler(0);
}

extern "C" __attribute__((interrupt)) void USB_LP_CAN1_RX0_IRQHandler(void)
{
    tud_int_handler(0);
}

extern "C" __attribute__((interrupt)) void USBWakeUp_IRQHandler(void)
{
    tud_int_handler(0);
}

/* ======================================================= CH32V303
 * USBFS (OTG_FS) on PA11 (D-) / PA12 (D+); V303 has no USBHS.
 * RCC_CFGR0[23:22] USBPRE: 00 = /1, 01 = /2, 10 = /3. */
#elif defined(CH32RV_SERIES_CH32V303)

#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define V2X_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define V2X_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define V2X_USBPRE 2u
#endif

static CH32RVUsbDeviceError port_open(CH32RVUsbController controller)
{
#ifdef V2X_USBPRE
    if (controller != CH32RVUsbController::Auto && controller != CH32RVUsbController::FullSpeed) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << CH32RV_USB_USBPRE_SHIFT)) |
                       (V2X_USBPRE << CH32RV_USB_USBPRE_SHIFT);
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
    ch32rv_clock_enable(USBFS);
    return CH32RVUsbDeviceError::None;
#else
    (void)controller;
    return CH32RVUsbDeviceError::PortUnavailable;
#endif
}

static void port_close(void)
{
    ch32rv_clock_disable(USBFS);
}

static uint8_t port_last_endpoint(void)
{
    return 7;
}

/* The USBFS vector is shared with CH32RVUsbHost (ch32rv_usbfs_irq.c). */
#define CH32RV_USB_SHARED_IRQ 1
static void port_irq(void)
{
    tud_int_handler(0);
}

/* ======================================================= CH32V305 / V307 / V317
 * USBFS (PA11 / PA12, 48 MHz from the PLL like V303) and USBHS (PB6 / PB7,
 * its own PHY). Both drivers are linked and ch32rv_dcd_dual.c forwards to the
 * chosen one; the device always runs as rhport 0.
 *
 * USBHS needs HSE: the HS PHY PLL takes a 3, 4, 5 or 8 MHz reference divided
 * from the crystal (RM: "the clock source of USBHSPLL can only be HSE").
 * CH32RV_HSE_HZ names the crystal (default 8 MHz, as on the EVT boards); a
 * board without one fails begin() with PortUnavailable when HighSpeed is
 * asked for. SYSCLK stays on HSI-PLL. */
#elif defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
      defined(CH32RV_SERIES_CH32V317)

#include "ch32rv_dcd_dual.h"

#ifndef CH32RV_HSE_HZ
#define CH32RV_HSE_HZ 8000000u
#endif
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define V30X_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define V30X_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define V30X_USBPRE 2u
#endif

#define V30X_RCC_CTLR_HSEON         (1u << 16)
#define V30X_RCC_CTLR_HSERDY        (1u << 17)
#define V30X_RCC_CFGR2              CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define V30X_CFGR2_USBFSSRC_PHY     (1u << 31)
#define V30X_CFGR2_USBHSPLL_ALIVE   (1u << 30)
#define V30X_CFGR2_USBHSCKREF_SHIFT 28u   /* 00 3 MHz, 01 4 MHz, 10 8 MHz, 11 5 MHz */
#define V30X_CFGR2_USBHSDIV_SHIFT   24u   /* HSE / (n + 1); bit 27 = 0: HSE source */
#define V30X_CFGR2_USB_MASK         0xFF000000u

static bool v30x_hse_on(void)
{
    if (CH32RV_RCC_CTLR & V30X_RCC_CTLR_HSERDY) {
        return true;
    }
    CH32RV_RCC_CTLR |= V30X_RCC_CTLR_HSEON;
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 20u) {        /* a crystal starts in a few ms */
        if (CH32RV_RCC_CTLR & V30X_RCC_CTLR_HSERDY) {
            return true;
        }
    }
    CH32RV_RCC_CTLR &= ~V30X_RCC_CTLR_HSEON;
    return false;
}

/* The CFGR2 bits for the HS PHY PLL from this crystal: 4 MHz reference first
 * (the EVT's 8 MHz / 2), then 8, 5, 3. 0 when none divides it. */
static uint32_t v30x_usbhs_pll_bits(uint32_t hse_hz)
{
    static const struct {
        uint32_t hz;
        uint32_t sel;
    } refs[] = {{4000000u, 1u}, {8000000u, 2u}, {5000000u, 3u}, {3000000u, 0u}};
    for (unsigned r = 0; r < sizeof refs / sizeof refs[0]; r++) {
        for (uint32_t div = 1; div <= 8; div++) {
            if (hse_hz == refs[r].hz * div) {
                return V30X_CFGR2_USBHSPLL_ALIVE | (refs[r].sel << V30X_CFGR2_USBHSCKREF_SHIFT) |
                       ((div - 1u) << V30X_CFGR2_USBHSDIV_SHIFT);
            }
        }
    }
    return 0;
}

static CH32RVUsbDeviceError port_open(CH32RVUsbController controller)
{
#if defined(CH32RV_PART_CH32V305FBP6) || defined(CH32RV_PART_CH32V305GBU6)
    /* These packages bring out only the USBHS pins. */
    if (controller == CH32RVUsbController::FullSpeed) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
    const bool hs = true;
#else
    const bool hs = controller == CH32RVUsbController::HighSpeed;
#endif
    if (controller == CH32RVUsbController::Usbd ||
        (!hs && controller != CH32RVUsbController::Auto && controller != CH32RVUsbController::FullSpeed)) {
        return CH32RVUsbDeviceError::PortUnavailable;
    }
    if (hs) {
        const uint32_t pll = v30x_usbhs_pll_bits(CH32RV_HSE_HZ);
        if (pll == 0 || !v30x_hse_on()) {
            return CH32RVUsbDeviceError::PortUnavailable;
        }
        V30X_RCC_CFGR2 = (V30X_RCC_CFGR2 & ~(V30X_CFGR2_USB_MASK & ~V30X_CFGR2_USBFSSRC_PHY)) | pll;
        ch32rv_clock_enable(USBHS);
        ch32rv_dcd_select(CH32RV_DCD_USBHS);
    } else {
#ifdef V30X_USBPRE
        CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << CH32RV_USB_USBPRE_SHIFT)) |
                           (V30X_USBPRE << CH32RV_USB_USBPRE_SHIFT);
        V30X_RCC_CFGR2 &= ~V30X_CFGR2_USBFSSRC_PHY;
        pinMode(PA11, INPUT);
        pinMode(PA12, INPUT);
        ch32rv_clock_enable(USBFS);
        ch32rv_dcd_select(CH32RV_DCD_USBFS);
#else
        return CH32RVUsbDeviceError::PortUnavailable;
#endif
    }
    port_hs = hs;
    return CH32RVUsbDeviceError::None;
}

static void port_close(void)
{
    if (port_hs) {
        ch32rv_clock_disable(USBHS);
        V30X_RCC_CFGR2 &= ~V30X_CFGR2_USBHSPLL_ALIVE;
    } else {
        ch32rv_clock_disable(USBFS);
    }
}

static uint8_t port_last_endpoint(void)
{
    return port_hs ? 15 : 7;
}

/* Both report as rhport 0 (ch32rv_dcd_dual.c). */
/* The USBFS vector is shared with CH32RVUsbHost (ch32rv_usbfs_irq.c). */
#define CH32RV_USB_SHARED_IRQ 1
static void port_irq(void)
{
    tud_int_handler(0);
}

extern "C" __attribute__((interrupt)) void USBHS_IRQHandler(void)
{
    tud_int_handler(0);
}

#else
#error "CH32RV_TINYUSB_DEVICE is set for a series without a USB port here"
#endif
