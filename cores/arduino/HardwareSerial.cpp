#include "HardwareSerial.h"

#include "Arduino.h"
#include "ch32_gpio.h"
#include "ch32_registers.h"

using namespace arduino;

void CH32HardwareSerial::begin(unsigned long baudrate, uint16_t config)
{
    if (_started) {
        end();
    }

    if (_on_apb1) {
        CH32_RCC_APB1PCENR |= _clock_bit;
    } else {
        CH32_RCC_APB2PCENR |= _clock_bit;
    }
    CH32_RCC_APB2PCENR |= CH32_RCC_APB2_AFIO;
    if (_remap_mask) {
        CH32_AFIO_PCFR1 = (CH32_AFIO_PCFR1 & ~_remap_mask) | _remap_value;
    }

    const uint8_t tx_port = (uint8_t)CH32_PIN_PORT(_tx_pin);
    const uint8_t rx_port = (uint8_t)CH32_PIN_PORT(_rx_pin);
    ch32_gpio_clock_enable(tx_port);
    ch32_gpio_clock_enable(rx_port);
    ch32_gpio_set_config(tx_port, (uint8_t)CH32_PIN_BIT(_tx_pin),
                         CH32_GPIO_CFG_AF_PP_50M);
    /* Pull the RX line up so an unconnected input idles as a mark instead of
     * generating framing errors. */
    ch32_gpio_set_config(rx_port, (uint8_t)CH32_PIN_BIT(_rx_pin),
                         CH32_GPIO_CFG_IN_PULL);
    ch32_gpio_set(rx_port, (uint8_t)CH32_PIN_BIT(_rx_pin));

    /* USART1 hangs off PCLK2 and the others off PCLK1; Milestone 1 leaves both
     * prescalers at /1, so either way the clock is F_CPU. BRR holds
     * USARTDIV * 16, which is exactly the rounded fck/baud. */
    CH32_USART_BRR(_base) = (uint16_t)((F_CPU + baudrate / 2) / baudrate);

    uint16_t ctlr1 = CH32_USART_CTLR1_TE | CH32_USART_CTLR1_RE |
                     CH32_USART_CTLR1_RXNEIE;
    uint16_t ctlr2 = 0;

    switch (config & SERIAL_PARITY_MASK) {
    case SERIAL_PARITY_EVEN: ctlr1 |= CH32_USART_CTLR1_PCE; break;
    case SERIAL_PARITY_ODD:  ctlr1 |= CH32_USART_CTLR1_PCE | CH32_USART_CTLR1_PS; break;
    default: break;
    }
    /* The parity bit occupies the ninth position, so 8 data bits with parity
     * means a 9-bit word. */
    if ((config & SERIAL_DATA_MASK) == SERIAL_DATA_8 &&
        (ctlr1 & CH32_USART_CTLR1_PCE)) {
        ctlr1 |= CH32_USART_CTLR1_M;
    }
    switch (config & SERIAL_STOP_BIT_MASK) {
    case SERIAL_STOP_BIT_2:   ctlr2 |= CH32_USART_CTLR2_STOP_2; break;
    case SERIAL_STOP_BIT_1_5: ctlr2 |= CH32_USART_CTLR2_STOP_1P5; break;
    default: break;
    }

    CH32_USART_CTLR2(_base) = ctlr2;
    CH32_USART_CTLR3(_base) = 0;
    CH32_USART_CTLR1(_base) = ctlr1 | CH32_USART_CTLR1_UE;

    ch32_irq_enable(_irqn);
    _started = true;
}

void CH32HardwareSerial::end(void)
{
    flush();
    ch32_irq_disable(_irqn);
    CH32_USART_CTLR1(_base) = 0;
    _rx.clear();
    _tx.clear();
    _started = false;
}

int CH32HardwareSerial::available(void)
{
    return _rx.available();
}

int CH32HardwareSerial::peek(void)
{
    return _rx.peek();
}

int CH32HardwareSerial::read(void)
{
    return _rx.read_char();
}

void CH32HardwareSerial::flush(void)
{
    if (!_started) {
        return;
    }
    while (_tx.available() > 0) {
    }
    while ((CH32_USART_STATR(_base) & CH32_USART_STATR_TC) == 0u) {
    }
}

void CH32HardwareSerial::start_tx(void)
{
    CH32_USART_CTLR1(_base) |= CH32_USART_CTLR1_TXEIE;
}

size_t CH32HardwareSerial::write(uint8_t c)
{
    if (!_started) {
        return 0;
    }
    /* Block until the ring has room. The TX interrupt is what drains it, so
     * this cannot deadlock as long as interrupts are enabled. */
    while (_tx.isFull()) {
    }
    _tx.store_char(c);
    start_tx();
    return 1;
}

void CH32HardwareSerial::irq(void)
{
    const uint16_t status = CH32_USART_STATR(_base);

    if (status & CH32_USART_STATR_RXNE) {
        _rx.store_char((uint8_t)CH32_USART_DATAR(_base));
    }
    if (status & CH32_USART_STATR_TXE) {
        if (_tx.available() == 0) {
            CH32_USART_CTLR1(_base) &= (uint16_t)~CH32_USART_CTLR1_TXEIE;
        } else {
            CH32_USART_DATAR(_base) = (uint16_t)(uint8_t)_tx.read_char();
        }
    }
}

/* ------------------------------------------------------- instances + ISRs */
/* The variant supplies the pins, the IRQ number and the handler symbol; the
 * handler is USARTn_IRQHandler on some families and UARTn_IRQHandler on others,
 * so the name comes from the generated vector table rather than from here. */
#define CH32_REMAP_MASK(n) CH32_SERIAL##n##_REMAP_MASK
#define CH32_REMAP_VAL(n)  CH32_SERIAL##n##_REMAP_VAL

#define CH32_DEFINE_SERIAL(n, base, apb1, clkbit, rmask, rval)                \
    arduino::CH32HardwareSerial Serial##n(base, CH32_SERIAL##n##_IRQ,         \
                                          CH32_SERIAL##n##_TX,                \
                                          CH32_SERIAL##n##_RX, apb1, clkbit,  \
                                          rmask, rval);                       \
    extern "C" __attribute__((interrupt))                                     \
    void CH32_SERIAL##n##_HANDLER(void) { Serial##n.irq(); }

#if defined(CH32_SERIAL1_TX)
#ifndef CH32_SERIAL1_REMAP_MASK
#define CH32_SERIAL1_REMAP_MASK 0u
#define CH32_SERIAL1_REMAP_VAL  0u
#endif
CH32_DEFINE_SERIAL(1, CH32_USART1_BASE, false, CH32_RCC_APB2_USART1,
                   CH32_SERIAL1_REMAP_MASK, CH32_SERIAL1_REMAP_VAL)
#endif
#if defined(CH32_SERIAL2_TX)
#ifndef CH32_SERIAL2_REMAP_MASK
#define CH32_SERIAL2_REMAP_MASK 0u
#define CH32_SERIAL2_REMAP_VAL  0u
#endif
CH32_DEFINE_SERIAL(2, CH32_USART2_BASE, true, CH32_RCC_APB1_USART2,
                   CH32_SERIAL2_REMAP_MASK, CH32_SERIAL2_REMAP_VAL)
#endif
#if defined(CH32_SERIAL3_TX)
#ifndef CH32_SERIAL3_REMAP_MASK
#define CH32_SERIAL3_REMAP_MASK 0u
#define CH32_SERIAL3_REMAP_VAL  0u
#endif
CH32_DEFINE_SERIAL(3, CH32_USART3_BASE, true, CH32_RCC_APB1_USART3,
                   CH32_SERIAL3_REMAP_MASK, CH32_SERIAL3_REMAP_VAL)
#endif
#if defined(CH32_SERIAL4_TX)
#ifndef CH32_SERIAL4_REMAP_MASK
#define CH32_SERIAL4_REMAP_MASK 0u
#define CH32_SERIAL4_REMAP_VAL  0u
#endif
CH32_DEFINE_SERIAL(4, CH32_USART4_BASE, true, CH32_RCC_APB1_USART4,
                   CH32_SERIAL4_REMAP_MASK, CH32_SERIAL4_REMAP_VAL)
#endif
#if defined(CH32_SERIAL5_TX)
#ifndef CH32_SERIAL5_REMAP_MASK
#define CH32_SERIAL5_REMAP_MASK 0u
#define CH32_SERIAL5_REMAP_VAL  0u
#endif
CH32_DEFINE_SERIAL(5, CH32_USART5_BASE, true, CH32_RCC_APB1_USART5,
                   CH32_SERIAL5_REMAP_MASK, CH32_SERIAL5_REMAP_VAL)
#endif

/* --------------------------------------------------------- printf() bridge */
#include "ch32_serial_write.h"

extern "C" size_t ch32_serial_write_bytes(const uint8_t *data, size_t len)
{
#ifdef SERIAL_PORT_MONITOR
    if (!SERIAL_PORT_MONITOR) {
        return 0;
    }
    return SERIAL_PORT_MONITOR.write(data, len);
#else
    (void)data;
    (void)len;
    return 0;
#endif
}
