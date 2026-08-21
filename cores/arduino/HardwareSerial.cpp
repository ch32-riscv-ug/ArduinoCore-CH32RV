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
    _baudrate = baudrate;
    _config = config;

    if (_on_apb1) {
        CH32_RCC_APB1PCENR |= _clock_bit;
    } else {
        CH32_RCC_APB2PCENR |= _clock_bit;
    }
    CH32_RCC_APB2PCENR |= CH32_RCC_APB2_AFIO;
    /* Written every time, including for the default route, so begin() does
     * not depend on what the field already held - going back to the default
     * pins is ordinary use, not an edge case. A zero mask means device-data
     * knows no field for this port, which is the only case left alone.
     *
     * The selector is also not always one field in one register: on L103/M103
     * and V20x/V30x it spans PCFR1 and PCFR2, and writing only PCFR1 selects a
     * different route with nothing to say so. The variant carries one mask per
     * register the field touches. */
    if (_remap_mask) {
        CH32_AFIO_PCFR1 = (CH32_AFIO_PCFR1 & ~_remap_mask) | _remap_value;
    }
    if (_remap2_mask) {
        CH32_AFIO_PCFR2 = (CH32_AFIO_PCFR2 & ~_remap2_mask) | _remap2_value;
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
     * APB prescalers at /1, so either way the clock is HCLK, which SystemInit
     * makes equal to F_CPU. BRR holds USARTDIV * 16, which is exactly the
     * rounded fck/baud - and getting it wrong is how a mis-set AHB prescaler
     * announces itself, as garbled output. */
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
    return _rx.pop();
}

void CH32HardwareSerial::flush(void)
{
    if (!_started) {
        return;
    }
    while (!_tx.isEmpty()) {
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
    _tx.push(c);
    start_tx();
    return 1;
}

void CH32HardwareSerial::irq(void)
{
    const uint16_t status = CH32_USART_STATR(_base);

    /* RXNEIE also raises the interrupt for the receive error flags, and those
     * are only cleared by reading STATR and then DATAR. Handling just RXNE
     * leaves an overrun asserted, the interrupt re-enters immediately and the
     * core never returns to loop() - which is what a noisy or unconnected RX
     * line produces. So always drain the data register when any receive flag
     * is set, and only keep the byte when it is a real one. */
    if (status & (CH32_USART_STATR_RXNE | CH32_USART_STATR_ORE |
                  CH32_USART_STATR_NE | CH32_USART_STATR_FE |
                  CH32_USART_STATR_PE)) {
        const uint8_t data = (uint8_t)CH32_USART_DATAR(_base);
        if (status & CH32_USART_STATR_RXNE) {
            _rx.push(data);
        }
    }
    if (status & CH32_USART_STATR_TXE) {
        if (_tx.isEmpty()) {
            CH32_USART_CTLR1(_base) &= (uint16_t)~CH32_USART_CTLR1_TXEIE;
        } else {
            CH32_USART_DATAR(_base) = (uint16_t)(uint8_t)_tx.pop();
        }
    }
}

/* --------------------------------------------------------------- routes */
/* The tables are reached only from setRoute()/setPins(), never from the
 * constructor, so a sketch that does not move its pins does not pay for them:
 * with -ffunction-sections/-fdata-sections and --gc-sections the unused
 * functions go, and the tables go with them. */
namespace {

struct RouteTable {
    const ch32_route_t *rows;
    uint8_t count;
};

RouteTable routes_for(uint32_t base)
{
#if defined(CH32_SERIAL1_ROUTES)
    static const ch32_route_t r1[] = CH32_SERIAL1_ROUTES;
    if (base == CH32_USART1_BASE) {
        return {r1, CH32_SERIAL1_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SERIAL2_ROUTES)
    static const ch32_route_t r2[] = CH32_SERIAL2_ROUTES;
    if (base == CH32_USART2_BASE) {
        return {r2, CH32_SERIAL2_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SERIAL3_ROUTES)
    static const ch32_route_t r3[] = CH32_SERIAL3_ROUTES;
    if (base == CH32_USART3_BASE) {
        return {r3, CH32_SERIAL3_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SERIAL4_ROUTES)
    static const ch32_route_t r4[] = CH32_SERIAL4_ROUTES;
    if (base == CH32_USART4_BASE) {
        return {r4, CH32_SERIAL4_ROUTE_COUNT};
    }
#endif
#if defined(CH32_SERIAL5_ROUTES)
    static const ch32_route_t r5[] = CH32_SERIAL5_ROUTES;
    if (base == CH32_USART5_BASE) {
        return {r5, CH32_SERIAL5_ROUTE_COUNT};
    }
#endif
    (void)base;
    return {nullptr, 0};
}

/* Hand a pad back as a floating input. Leaving the old TX configured as an
 * alternate-function output would keep it driving after the port moved away
 * from it. */
void release_pin(uint8_t pin)
{
    ch32_gpio_set_config((uint8_t)CH32_PIN_PORT(pin), (uint8_t)CH32_PIN_BIT(pin),
                         CH32_GPIO_CFG_IN_FLOAT);
}

}  // namespace

bool CH32HardwareSerial::use_route(const ch32_route_t &route)
{
    const uint8_t old_tx = _tx_pin;
    const uint8_t old_rx = _rx_pin;
    const bool was_started = _started;
    const unsigned long baudrate = _baudrate;
    const uint16_t config = _config;

    if (was_started) {
        end();
        release_pin(old_tx);
        release_pin(old_rx);
    }
    _tx_pin = route.pins[0];
    _rx_pin = route.pins[1];
    _remap_value = route.value;
    _remap2_value = route.value2;
    if (was_started) {
        begin(baudrate, config);
    }
    return true;
}

bool CH32HardwareSerial::setRoute(uint8_t route)
{
    const RouteTable table = routes_for(_base);
    const int i = ch32_route_find(table.rows, table.count, route);
    if (i < 0) {
        return false;
    }
    return use_route(table.rows[i]);
}

bool CH32HardwareSerial::setPins(uint8_t tx, uint8_t rx)
{
    const RouteTable table = routes_for(_base);
    const uint8_t want[CH32_ROUTE_PINS] = {tx, rx, CH32_ROUTE_NO_PIN};
    const int i = ch32_route_match(table.rows, table.count, want);
    if (i < 0) {
        return false;
    }
    return use_route(table.rows[i]);
}

/* ------------------------------------------------------- instances + ISRs */
/* The variant supplies the pins, the IRQ number and the handler symbol; the
 * handler is USARTn_IRQHandler on some families and UARTn_IRQHandler on others,
 * so the name comes from the generated vector table rather than from here. */
#define CH32_DEFINE_SERIAL(n, base, apb1, clkbit)                             \
    arduino::CH32HardwareSerial Serial##n(base, CH32_SERIAL##n##_IRQ,         \
                                          CH32_SERIAL##n##_TX,                \
                                          CH32_SERIAL##n##_RX, apb1, clkbit,  \
                                          CH32_SERIAL##n##_REMAP_MASK,        \
                                          CH32_SERIAL##n##_REMAP_VAL,         \
                                          CH32_SERIAL##n##_REMAP2_MASK,       \
                                          CH32_SERIAL##n##_REMAP2_VAL);       \
    extern "C" __attribute__((interrupt))                                     \
    void CH32_SERIAL##n##_HANDLER(void) { Serial##n.irq(); }

#if defined(CH32_SERIAL1_TX)
#ifndef CH32_SERIAL1_REMAP_MASK
#define CH32_SERIAL1_REMAP_MASK 0u
#define CH32_SERIAL1_REMAP_VAL  0u
#endif
#ifndef CH32_SERIAL1_REMAP2_MASK
#define CH32_SERIAL1_REMAP2_MASK 0u
#define CH32_SERIAL1_REMAP2_VAL  0u
#endif
CH32_DEFINE_SERIAL(1, CH32_USART1_BASE, false, CH32_RCC_APB2_USART1)
#endif
#if defined(CH32_SERIAL2_TX)
#ifndef CH32_SERIAL2_REMAP_MASK
#define CH32_SERIAL2_REMAP_MASK 0u
#define CH32_SERIAL2_REMAP_VAL  0u
#endif
#ifndef CH32_SERIAL2_REMAP2_MASK
#define CH32_SERIAL2_REMAP2_MASK 0u
#define CH32_SERIAL2_REMAP2_VAL  0u
#endif
CH32_DEFINE_SERIAL(2, CH32_USART2_BASE, true, CH32_RCC_APB1_USART2)
#endif
#if defined(CH32_SERIAL3_TX)
#ifndef CH32_SERIAL3_REMAP_MASK
#define CH32_SERIAL3_REMAP_MASK 0u
#define CH32_SERIAL3_REMAP_VAL  0u
#endif
#ifndef CH32_SERIAL3_REMAP2_MASK
#define CH32_SERIAL3_REMAP2_MASK 0u
#define CH32_SERIAL3_REMAP2_VAL  0u
#endif
CH32_DEFINE_SERIAL(3, CH32_USART3_BASE, true, CH32_RCC_APB1_USART3)
#endif
#if defined(CH32_SERIAL4_TX)
#ifndef CH32_SERIAL4_REMAP_MASK
#define CH32_SERIAL4_REMAP_MASK 0u
#define CH32_SERIAL4_REMAP_VAL  0u
#endif
#ifndef CH32_SERIAL4_REMAP2_MASK
#define CH32_SERIAL4_REMAP2_MASK 0u
#define CH32_SERIAL4_REMAP2_VAL  0u
#endif
CH32_DEFINE_SERIAL(4, CH32_USART4_BASE, true, CH32_RCC_APB1_USART4)
#endif
#if defined(CH32_SERIAL5_TX)
#ifndef CH32_SERIAL5_REMAP_MASK
#define CH32_SERIAL5_REMAP_MASK 0u
#define CH32_SERIAL5_REMAP_VAL  0u
#endif
#ifndef CH32_SERIAL5_REMAP2_MASK
#define CH32_SERIAL5_REMAP2_MASK 0u
#define CH32_SERIAL5_REMAP2_VAL  0u
#endif
CH32_DEFINE_SERIAL(5, CH32_USART5_BASE, true, CH32_RCC_APB1_USART5)
#endif

/* --------------------------------------------------------- printf() bridge */
#include "ch32_serial_write.h"

/* Where printf()/puts() go. A pointer rather than a compile-time choice,
 * because the alternatives to the UART - SDI print, and later USB CDC - live
 * in libraries, and the core must not have to know about them to let a sketch
 * pick one. The default is the board's monitor port, resolved at link time, so
 * a sketch that never calls ch32_set_stdout() behaves exactly as before.
 *
 * A port that has not been begun accepts nothing: HardwareSerial::write()
 * returns 0 when it is closed, so printf() before Serial.begin() stays a
 * silent no-op rather than a hang. */
static Print *ch32_stdout =
#ifdef SERIAL_PORT_MONITOR
    &SERIAL_PORT_MONITOR;
#else
    nullptr;
#endif

void ch32_set_stdout(Print *out)
{
    ch32_stdout = out;
}

Print *ch32_get_stdout(void)
{
    return ch32_stdout;
}

extern "C" size_t ch32_serial_write_bytes(const uint8_t *data, size_t len)
{
    return ch32_stdout ? ch32_stdout->write(data, len) : 0;
}
