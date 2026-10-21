/* CH32RVHsAdc.h. Registers from CH32H417RM chapter 12 (HSADC, 0x40017400)
 * and the EVT's HSADC example; RCC_CFGR2 HSADCSRC [29:28]. */
#include "CH32RVHsAdc.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"

#define HSADC_CFGR  CH32RV_REG32(0x40017400u)
#define HSADC_CTLR1 CH32RV_REG32(0x40017404u)
#define HSADC_CTLR2 CH32RV_REG32(0x40017408u)
#define HSADC_STATR CH32RV_REG32(0x4001740Cu)
#define HSADC_DATAR CH32RV_REG32(0x40017410u)
#define HSADC_ADDR0 CH32RV_REG32(0x40017414u)
#define HSADC_ADDR1 CH32RV_REG32(0x40017418u)
#define RCC_CFGR2   CH32RV_REG32(0x40021030u)

#define CFGR_EN       (1u << 0)
#define CFGR_DMAEN    (1u << 1)
#define CFGR_PPMODE   (1u << 14)
#define CTLR1_START   (1u << 0)
#define CTLR1_DMAIE   (1u << 9)
#define STATR_EOCIF   (1u << 0)
#define STATR_DMAIF   (1u << 1)
#define STATR_RXNE    (1u << 3)
#define STATR_PP_ADDR (1u << 4)

/* The HSADC clock: PLL_CLK. Its frequency is SYSCLK's (the core runs SYSCLK
 * from the PLL on these parts). */
#define HSADC_SRC_HZ  CH32RV_CLOCK_SYSCLK_HZ
#define HSADC_MAX_HZ  100000000UL          /* datasheet fHSADC */

static CH32RVHsAdc *active;

int CH32RVHsAdc::channelOf(pin_size_t pin)
{
    switch (pin) {
    case PC0: return 0;
    case PC1: return 1;
    case PC2: return 2;
    case PC3: return 3;
#ifdef PF8                                             /* not bonded out on every package */
    case PF8: return 4;
#endif
#ifdef PF9
    case PF9: return 5;
#endif
#ifdef PF10
    case PF10: return 6;
#endif
    default: return -1;
    }
}

bool CH32RVHsAdc::begin(pin_size_t pin, uint32_t sampleRate)
{
    const int ch = channelOf(pin);
    if (ch < 0 || sampleRate == 0) {
        return false;
    }
    stop();
    /* The smallest divider whose clock stays at or under 100 MHz and whose
     * rate is at or under the one asked for. */
    uint32_t div = 0;
    while (div < 63u && ((HSADC_SRC_HZ / (div + 1u)) > HSADC_MAX_HZ ||
                         HSADC_SRC_HZ / (div + 1u) / 5u > sampleRate)) {
        div++;
    }
    _div = (uint8_t)div;
    _rate = HSADC_SRC_HZ / (div + 1u) / 5u;
    _channel = (uint8_t)ch;

    ch32rv_clock_enable(HSADC);
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin);
    ch32rv_gpio_clock_enable(port);
    ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pin), CH32RV_GPIO_CFG_IN_ANALOG);
    RCC_CFGR2 = (RCC_CFGR2 & ~(3u << 28)) | (1u << 28);   /* HSADCSRC = PLL_CLK */
    HSADC_CTLR1 = 0;
    HSADC_CFGR = 0;
    HSADC_STATR = 7u;
    _begun = true;
    active = this;
    return true;
}

void CH32RVHsAdc::end()
{
    stop();
    HSADC_CFGR = 0;
    _begun = false;
    if (active == this) {
        active = nullptr;
    }
}

uint16_t CH32RVHsAdc::read()
{
    if (!_begun || _running) {
        return 0;
    }
    /* Continuous conversion is the only mode, and on the bench (2026-10-21)
     * neither the data register's flag nor a plain DMA run gives a bounded
     * single reading: without DMA the flag comes once and no more, and DMA
     * without ping-pong kept writing past DMA_LEN. Ping-pong keeps every
     * write inside its two buffers whatever happens, so: two small buffers,
     * wait for the first, stop, keep its last sample (the first ones carry
     * the setup cycles). The wait has a timeout, not a hang. */
    static uint16_t few[2][8] __attribute__((aligned(4)));
    HSADC_CTLR1 = 0;
    HSADC_STATR = 7u;
    few[0][7] = 0xFFFFu;
    HSADC_ADDR0 = (uint32_t)(uintptr_t)few[0];
    HSADC_ADDR1 = (uint32_t)(uintptr_t)few[1];
    HSADC_CFGR = ((uint32_t)8u << 16) | CFGR_PPMODE | ((uint32_t)_div << 8) | ((uint32_t)_channel << 2) |
                 CFGR_DMAEN;
    HSADC_CFGR |= CFGR_EN;
    delayMicroseconds(2);                              /* tSTAB, 1 us */
    HSADC_CTLR1 = CTLR1_START;
    const uint32_t t0 = micros();
    while (!(HSADC_STATR & STATR_DMAIF)) {
        if ((uint32_t)(micros() - t0) > 100u) {        /* 8 samples take under 7 us */
            break;
        }
    }
    HSADC_CTLR1 = 0;
    HSADC_CFGR = 0;
    HSADC_STATR = 7u;
    return (uint16_t)(few[0][7] & 0x3FFu);
}

bool CH32RVHsAdc::startContinuous(uint16_t *buf0, uint16_t *buf1, uint16_t samples)
{
    if (!_begun || !buf0 || !buf1 || samples == 0) {
        return false;
    }
    stop();
    _buf[0] = buf0;
    _buf[1] = buf1;
    _samples = samples;
    _ready = 0;
    _overruns = _filled = 0;
    HSADC_ADDR0 = (uint32_t)(uintptr_t)buf0;
    HSADC_ADDR1 = (uint32_t)(uintptr_t)buf1;
    HSADC_CTLR2 = 0;
    HSADC_STATR = 7u;
    /* DMA_LEN, ping-pong, divider, 16-bit, channel, DMA */
    HSADC_CFGR = ((uint32_t)samples << 16) | CFGR_PPMODE | ((uint32_t)_div << 8) | ((uint32_t)_channel << 2) |
                 CFGR_DMAEN;
    HSADC_CFGR |= CFGR_EN;
    delayMicroseconds(2);
    _running = true;
    ch32rv_irq_enable(CH32RV_IRQN_HSADC);
    HSADC_CTLR1 = CTLR1_DMAIE | CTLR1_START;
    return true;
}

void CH32RVHsAdc::stop()
{
    if (!_running) {
        return;
    }
    HSADC_CTLR1 = 0;
    ch32rv_irq_disable(CH32RV_IRQN_HSADC);
    HSADC_CFGR = 0;
    HSADC_STATR = 7u;
    _running = false;
}

const uint16_t *CH32RVHsAdc::readBuffer()
{
    noInterrupts();
    const uint8_t r = _ready;
    uint8_t take = 0xFF;
    if (r) {
        /* The older one first when both are waiting (the newer is then
         * being overwritten - already counted). */
        take = (r == 3u) ? (uint8_t)(_last ^ 1u) : (uint8_t)(r == 1u ? 0u : 1u);
        _ready = (uint8_t)(r & ~(1u << take));
    }
    interrupts();
    return take == 0xFF ? nullptr : _buf[take];
}

void CH32RVHsAdc::isr()
{
    const uint32_t st = HSADC_STATR;
    if (!(st & STATR_DMAIF)) {
        HSADC_STATR = st & 7u;
        return;
    }
    HSADC_STATR = STATR_DMAIF;
    /* PP_ADDR names the buffer the DMA writes now: the other one is full. */
    const uint8_t full = (st & STATR_PP_ADDR) ? 0u : 1u;
    if (_ready & (1u << full)) {
        _overruns++;                                   /* the sketch still had not taken it */
    }
    _ready |= (uint8_t)(1u << full);
    _last = full;
    _filled++;
}

extern "C" __attribute__((interrupt)) void HSADC_IRQHandler(void)
{
    if (active) {
        active->isr();
    } else {
        HSADC_STATR = 7u;
    }
}

#endif
