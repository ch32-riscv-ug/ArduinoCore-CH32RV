/* The modes of HardwareSerial beyond plain UART (HardwareSerial.h):
 * hardware flow control (setHwFlowCtrlMode()), RS485's driver enable
 * (setMode(UART_MODE_RS485_HALF_DUPLEX)), single-wire half duplex
 * (setPins(p, p)) and the receive events (onReceive(), setRxTimeout(),
 * setRxFIFOFull()), with ESP32's names; and the multiprocessor mute mode
 * (setAddress(), writeAddress(), mute()), the USART's own.
 *
 * In a file of its own, and reached from HardwareSerial.cpp only through
 * ch32rv_serial_mode_ops, which only this file writes: a sketch that uses none
 * of these links none of it, and link-time optimisation removes the hooks'
 * tests from begin(), end(), read(), write() and the interrupt. Once one is
 * used, the interrupt is this file's (CH32RVSerialMode::irq), the plain one's
 * two halves (internal/ch32rv_serial_irq.h) plus what the modes add.
 *
 * Why the core and not the USART: no CH32 USART drives a driver-enable pin
 * (device-data registers.csv: CTLR3 has CTSE, RTSE and HDSEL, no DEM on any
 * family), and its RTS only follows the data register - which this core's
 * interrupt empties at once into the receive buffer. So RS485's DE is a GPIO
 * raised before a write's first byte and dropped by the transmit-complete
 * interrupt, and RTS flow control stops taking bytes out of the data register
 * once the buffer holds the threshold: the USART then keeps the next byte and
 * deasserts RTS at its stop bit, until read() makes room.
 *
 * CTS / RTS pads come from the variant's CH32RV_SERIALn_CTS_ROUTES /
 * _RTS_ROUTES (tools/generate/generate.py, device-data's pinout): on the
 * remap families the row of the route's field value, on the per-pin families
 * (CH32RV_GPIO_PER_PIN_AF) any row, its value the pad's function number. */
#include "HardwareSerial.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_serial_irq.h"

using namespace arduino;

#if CH32RV_SERIAL_COUNT > 0

namespace {

/* ------------------------------------------------- CTS / RTS pads per USART */
/* One entry per enabled USART, in HardwareSerial.cpp's table order (its
 * CH32RV_USART_HAS_n are exactly these CH32RV_USART_ON_n). */
struct FlowSet {
    const ch32rv_route_t *cts;
    uint8_t cts_count;
    const ch32rv_route_t *rts;
    uint8_t rts_count;
};

#if CH32RV_USART_ON_1
#ifdef CH32RV_SERIAL1_CTS_ROUTES
const ch32rv_route_t usart1_cts[] = CH32RV_SERIAL1_CTS_ROUTES;
#define CH32RV_FLOW_CTS_1 usart1_cts, (uint8_t)(sizeof usart1_cts / sizeof usart1_cts[0])
#else
#define CH32RV_FLOW_CTS_1 nullptr, 0
#endif
#ifdef CH32RV_SERIAL1_RTS_ROUTES
const ch32rv_route_t usart1_rts[] = CH32RV_SERIAL1_RTS_ROUTES;
#define CH32RV_FLOW_RTS_1 usart1_rts, (uint8_t)(sizeof usart1_rts / sizeof usart1_rts[0])
#else
#define CH32RV_FLOW_RTS_1 nullptr, 0
#endif
#define CH32RV_FLOW_SET_1 { CH32RV_FLOW_CTS_1, CH32RV_FLOW_RTS_1 },
#else
#define CH32RV_FLOW_SET_1
#endif
#if CH32RV_USART_ON_2
#ifdef CH32RV_SERIAL2_CTS_ROUTES
const ch32rv_route_t usart2_cts[] = CH32RV_SERIAL2_CTS_ROUTES;
#define CH32RV_FLOW_CTS_2 usart2_cts, (uint8_t)(sizeof usart2_cts / sizeof usart2_cts[0])
#else
#define CH32RV_FLOW_CTS_2 nullptr, 0
#endif
#ifdef CH32RV_SERIAL2_RTS_ROUTES
const ch32rv_route_t usart2_rts[] = CH32RV_SERIAL2_RTS_ROUTES;
#define CH32RV_FLOW_RTS_2 usart2_rts, (uint8_t)(sizeof usart2_rts / sizeof usart2_rts[0])
#else
#define CH32RV_FLOW_RTS_2 nullptr, 0
#endif
#define CH32RV_FLOW_SET_2 { CH32RV_FLOW_CTS_2, CH32RV_FLOW_RTS_2 },
#else
#define CH32RV_FLOW_SET_2
#endif
#if CH32RV_USART_ON_3
#ifdef CH32RV_SERIAL3_CTS_ROUTES
const ch32rv_route_t usart3_cts[] = CH32RV_SERIAL3_CTS_ROUTES;
#define CH32RV_FLOW_CTS_3 usart3_cts, (uint8_t)(sizeof usart3_cts / sizeof usart3_cts[0])
#else
#define CH32RV_FLOW_CTS_3 nullptr, 0
#endif
#ifdef CH32RV_SERIAL3_RTS_ROUTES
const ch32rv_route_t usart3_rts[] = CH32RV_SERIAL3_RTS_ROUTES;
#define CH32RV_FLOW_RTS_3 usart3_rts, (uint8_t)(sizeof usart3_rts / sizeof usart3_rts[0])
#else
#define CH32RV_FLOW_RTS_3 nullptr, 0
#endif
#define CH32RV_FLOW_SET_3 { CH32RV_FLOW_CTS_3, CH32RV_FLOW_RTS_3 },
#else
#define CH32RV_FLOW_SET_3
#endif
#if CH32RV_USART_ON_4
#ifdef CH32RV_SERIAL4_CTS_ROUTES
const ch32rv_route_t usart4_cts[] = CH32RV_SERIAL4_CTS_ROUTES;
#define CH32RV_FLOW_CTS_4 usart4_cts, (uint8_t)(sizeof usart4_cts / sizeof usart4_cts[0])
#else
#define CH32RV_FLOW_CTS_4 nullptr, 0
#endif
#ifdef CH32RV_SERIAL4_RTS_ROUTES
const ch32rv_route_t usart4_rts[] = CH32RV_SERIAL4_RTS_ROUTES;
#define CH32RV_FLOW_RTS_4 usart4_rts, (uint8_t)(sizeof usart4_rts / sizeof usart4_rts[0])
#else
#define CH32RV_FLOW_RTS_4 nullptr, 0
#endif
#define CH32RV_FLOW_SET_4 { CH32RV_FLOW_CTS_4, CH32RV_FLOW_RTS_4 },
#else
#define CH32RV_FLOW_SET_4
#endif
#if CH32RV_USART_ON_5
#ifdef CH32RV_SERIAL5_CTS_ROUTES
const ch32rv_route_t usart5_cts[] = CH32RV_SERIAL5_CTS_ROUTES;
#define CH32RV_FLOW_CTS_5 usart5_cts, (uint8_t)(sizeof usart5_cts / sizeof usart5_cts[0])
#else
#define CH32RV_FLOW_CTS_5 nullptr, 0
#endif
#ifdef CH32RV_SERIAL5_RTS_ROUTES
const ch32rv_route_t usart5_rts[] = CH32RV_SERIAL5_RTS_ROUTES;
#define CH32RV_FLOW_RTS_5 usart5_rts, (uint8_t)(sizeof usart5_rts / sizeof usart5_rts[0])
#else
#define CH32RV_FLOW_RTS_5 nullptr, 0
#endif
#define CH32RV_FLOW_SET_5 { CH32RV_FLOW_CTS_5, CH32RV_FLOW_RTS_5 },
#else
#define CH32RV_FLOW_SET_5
#endif
#if CH32RV_USART_ON_6
#ifdef CH32RV_SERIAL6_CTS_ROUTES
const ch32rv_route_t usart6_cts[] = CH32RV_SERIAL6_CTS_ROUTES;
#define CH32RV_FLOW_CTS_6 usart6_cts, (uint8_t)(sizeof usart6_cts / sizeof usart6_cts[0])
#else
#define CH32RV_FLOW_CTS_6 nullptr, 0
#endif
#ifdef CH32RV_SERIAL6_RTS_ROUTES
const ch32rv_route_t usart6_rts[] = CH32RV_SERIAL6_RTS_ROUTES;
#define CH32RV_FLOW_RTS_6 usart6_rts, (uint8_t)(sizeof usart6_rts / sizeof usart6_rts[0])
#else
#define CH32RV_FLOW_RTS_6 nullptr, 0
#endif
#define CH32RV_FLOW_SET_6 { CH32RV_FLOW_CTS_6, CH32RV_FLOW_RTS_6 },
#else
#define CH32RV_FLOW_SET_6
#endif
#if CH32RV_USART_ON_7
#ifdef CH32RV_SERIAL7_CTS_ROUTES
const ch32rv_route_t usart7_cts[] = CH32RV_SERIAL7_CTS_ROUTES;
#define CH32RV_FLOW_CTS_7 usart7_cts, (uint8_t)(sizeof usart7_cts / sizeof usart7_cts[0])
#else
#define CH32RV_FLOW_CTS_7 nullptr, 0
#endif
#ifdef CH32RV_SERIAL7_RTS_ROUTES
const ch32rv_route_t usart7_rts[] = CH32RV_SERIAL7_RTS_ROUTES;
#define CH32RV_FLOW_RTS_7 usart7_rts, (uint8_t)(sizeof usart7_rts / sizeof usart7_rts[0])
#else
#define CH32RV_FLOW_RTS_7 nullptr, 0
#endif
#define CH32RV_FLOW_SET_7 { CH32RV_FLOW_CTS_7, CH32RV_FLOW_RTS_7 },
#else
#define CH32RV_FLOW_SET_7
#endif
#if CH32RV_USART_ON_8
#ifdef CH32RV_SERIAL8_CTS_ROUTES
const ch32rv_route_t usart8_cts[] = CH32RV_SERIAL8_CTS_ROUTES;
#define CH32RV_FLOW_CTS_8 usart8_cts, (uint8_t)(sizeof usart8_cts / sizeof usart8_cts[0])
#else
#define CH32RV_FLOW_CTS_8 nullptr, 0
#endif
#ifdef CH32RV_SERIAL8_RTS_ROUTES
const ch32rv_route_t usart8_rts[] = CH32RV_SERIAL8_RTS_ROUTES;
#define CH32RV_FLOW_RTS_8 usart8_rts, (uint8_t)(sizeof usart8_rts / sizeof usart8_rts[0])
#else
#define CH32RV_FLOW_RTS_8 nullptr, 0
#endif
#define CH32RV_FLOW_SET_8 { CH32RV_FLOW_CTS_8, CH32RV_FLOW_RTS_8 },
#else
#define CH32RV_FLOW_SET_8
#endif
#if CH32RV_USART_ON_9
#ifdef CH32RV_SERIAL9_CTS_ROUTES
const ch32rv_route_t usart9_cts[] = CH32RV_SERIAL9_CTS_ROUTES;
#define CH32RV_FLOW_CTS_9 usart9_cts, (uint8_t)(sizeof usart9_cts / sizeof usart9_cts[0])
#else
#define CH32RV_FLOW_CTS_9 nullptr, 0
#endif
#ifdef CH32RV_SERIAL9_RTS_ROUTES
const ch32rv_route_t usart9_rts[] = CH32RV_SERIAL9_RTS_ROUTES;
#define CH32RV_FLOW_RTS_9 usart9_rts, (uint8_t)(sizeof usart9_rts / sizeof usart9_rts[0])
#else
#define CH32RV_FLOW_RTS_9 nullptr, 0
#endif
#define CH32RV_FLOW_SET_9 { CH32RV_FLOW_CTS_9, CH32RV_FLOW_RTS_9 },
#else
#define CH32RV_FLOW_SET_9
#endif
#if CH32RV_USART_ON_10
#ifdef CH32RV_SERIAL10_CTS_ROUTES
const ch32rv_route_t usart10_cts[] = CH32RV_SERIAL10_CTS_ROUTES;
#define CH32RV_FLOW_CTS_10 usart10_cts, (uint8_t)(sizeof usart10_cts / sizeof usart10_cts[0])
#else
#define CH32RV_FLOW_CTS_10 nullptr, 0
#endif
#ifdef CH32RV_SERIAL10_RTS_ROUTES
const ch32rv_route_t usart10_rts[] = CH32RV_SERIAL10_RTS_ROUTES;
#define CH32RV_FLOW_RTS_10 usart10_rts, (uint8_t)(sizeof usart10_rts / sizeof usart10_rts[0])
#else
#define CH32RV_FLOW_RTS_10 nullptr, 0
#endif
#define CH32RV_FLOW_SET_10 { CH32RV_FLOW_CTS_10, CH32RV_FLOW_RTS_10 },
#else
#define CH32RV_FLOW_SET_10
#endif

const FlowSet flow_sets[] = {
    CH32RV_FLOW_SET_1
    CH32RV_FLOW_SET_2
    CH32RV_FLOW_SET_3
    CH32RV_FLOW_SET_4
    CH32RV_FLOW_SET_5
    CH32RV_FLOW_SET_6
    CH32RV_FLOW_SET_7
    CH32RV_FLOW_SET_8
    CH32RV_FLOW_SET_9
    CH32RV_FLOW_SET_10
    {nullptr, 0, nullptr, 0},   /* keeps the array non-empty */
};

/* The pad (and its function number) of a CTS / RTS row for the route TX / RX
 * are on; `want` 0 takes the first. 0 when there is none. */
struct Pad {
    uint8_t pin;
    uint8_t af;
};

Pad flow_pad(const ch32rv_route_t *rows, uint8_t count, const ch32rv_route_t &route, uint8_t want)
{
    for (uint8_t i = 0; i < count; i++) {
        const ch32rv_route_t &row = rows[i];
#if !CH32RV_GPIO_PER_PIN_AF
        /* One AFIO field moves TX, RX, CTS and RTS together: the row of the
         * route's own value. */
        if (row.value != route.value || row.value2 != route.value2) {
            continue;
        }
#else
        (void)route;
#endif
        if (!want || row.pins[0] == want) {
            return {row.pins[0], (uint8_t)row.value};
        }
    }
    return {0, 0};
}

/* ------------------------------------------------- per port */
enum : uint8_t {
    P_FULL = 1u << 0,      /* the receive buffer reached setRxFIFOFull()'s count */
    P_IDLE = 1u << 1,      /* the line went quiet after a byte (STATR IDLE)      */
};

struct Mode {
    /* What the sketch asked for. Pins 0 = none (no pad's number is 0). */
    uint8_t cts, rts;          /* setPins()' CTS / RTS                       */
    uint8_t flow;              /* SerialHwFlowCtrl                           */
    uint8_t threshold;         /* RTS: hold from this many bytes; 0 = all    */
    uint8_t mode;              /* SerialMode                                 */
    uint8_t only_timeout;
    uint8_t timeout_given, timeout;   /* setRxTimeout(); not given = 1      */
    uint8_t fifo_full;         /* setRxFIFOFull(); 0 = half the buffer       */
    /* What start() put on the pads, for end() and the interrupt. */
    uint8_t cts_on, rts_on, de;
    volatile uint8_t held;     /* RTS: a byte left in the data register      */
    volatile uint8_t pending;  /* P_*, for the delivery                      */
    /* Receive by DMA: its place at the IDLE (in the padding before idle_at). */
    volatile uint16_t idle_pos;
    volatile uint32_t idle_at; /* micros() at the IDLE                       */
    CH32RVHardwareSerial *port;
    CH32RVCallback<void(void)> on_receive;
    ch32rv_route_t one_wire;   /* setPins(p, p)'s route: RX joined to TX     */
    uint8_t addressed, address;   /* setAddress(): on, and its 0 .. 15     */
};
Mode modes[CH32RV_SERIAL_COUNT];

constexpr uint8_t CAPACITY = CH32RV_SERIAL_RX_BUFFER_SIZE - 1 > 255 ? 255
                                                                    : CH32RV_SERIAL_RX_BUFFER_SIZE - 1;

inline bool has_parity(uint16_t config)
{
    const uint16_t parity = config & SERIAL_PARITY_MASK;
    return parity && parity != SERIAL_PARITY_NONE;
}

/* setAddress()'s extra bit after the data: bit 8 of a 9-bit word for 8 data
 * bits, bit 7 of an 8-bit one for 7. */
inline uint16_t address_mark(uint16_t config)
{
    return (config & SERIAL_DATA_MASK) == SERIAL_DATA_8 ? 0x100u : 0x80u;
}

/* The port's format leaves room for setAddress()'s mark: no parity, 7 or 8
 * data bits. */
inline bool address_format(uint16_t config)
{
    const uint16_t data = config & SERIAL_DATA_MASK;
    return !has_parity(config) && (data == SERIAL_DATA_8 || data == SERIAL_DATA_7);
}

/* setAddress() on, and the port's format takes it. */
inline bool addressing(const Mode &m, uint16_t config)
{
    return m.addressed && address_format(config);
}

/* setAddress()'s part of start() and of the interrupt, reached only through
 * this pointer, which only setAddress() writes: a sketch that never calls it
 * links none of it (the other modes' users do not pay for it). */
struct AddressOps {
    /* CTLR1 as start() is about to write it -> with the word and the wake;
     * CTLR2's address set. */
    uint16_t (*format)(CH32RVHardwareSerial &port, uint32_t base, uint16_t ctlr1);
    /* The received frame taken when the port is addressed; false: not, the
     * plain path's. */
    bool (*receive)(CH32RVHardwareSerial &port, uint32_t base, uint32_t status);
};
const AddressOps *address_ops;

inline uint8_t symbols(const Mode &m)
{
    return m.timeout_given ? m.timeout : 1u;
}

inline uint8_t hold_at(const Mode &m)
{
    return m.threshold && m.threshold < CAPACITY ? m.threshold : CAPACITY;
}

inline uint8_t full_at(const Mode &m)
{
    return m.fifo_full ? m.fifo_full : (uint8_t)((CAPACITY + 1u) / 2u);
}

void pad_setup(uint8_t pin, uint32_t cfg, bool up, uint32_t af)
{
    const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pin), bit = (uint8_t)CH32RV_PIN_BIT(pin);
    ch32rv_gpio_clock_enable(port);
    if (up) {
        ch32rv_gpio_set(port, bit);
    }
    ch32rv_gpio_set_config(port, bit, cfg);
    ch32rv_pin_set_af(pin, af);
}

/* Let a pad go: a floating input on no function, as end() leaves TX / RX. */
void pad_release(uint8_t pin)
{
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_IN_FLOAT);
    ch32rv_pin_set_af(pin, 0);
}

inline void de_write(uint8_t pin, bool high)
{
    if (high) {
        ch32rv_gpio_set((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin));
    } else {
        ch32rv_gpio_clear((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin));
    }
}

/* A pin number as setPins() takes it, resolved: 0 for none or no pad. */
uint8_t resolve(int pin)
{
    if (pin < 0 || pin > 0xFE) {
        return 0;
    }
    const uint8_t pad = (uint8_t)CH32RV_PIN_RESOLVE(pin);
    return ch32rv_pad_is_valid(pad) ? pad : 0;
}

const CH32RVSerialModeOps mode_ops = {
    CH32RVSerialMode::start, CH32RVSerialMode::end, CH32RVSerialMode::irq,
    CH32RVSerialMode::read, CH32RVSerialMode::write, CH32RVSerialMode::written,
};

/* From here on the port's begin(), end(), read(), write() and interrupt go
 * through this file. */
inline void install(void)
{
    ch32rv_serial_mode_ops = &mode_ops;
}

}  // namespace

/* ------------------------------------------------- the hooks */
bool CH32RVSerialMode::flowPads(CH32RVHardwareSerial &port, uint8_t flow, uint8_t &cts, uint8_t &rts,
                                uint8_t &cts_af, uint8_t &rts_af)
{
    const Mode &m = modes[port._number];
    uint8_t index;
    const ch32rv_route_t *route = port.plannedRoute(index);
    cts = rts = 0;
    if (!route) {
        return false;
    }
    const FlowSet &set = flow_sets[index];
    if (flow & UART_HW_FLOWCTRL_CTS) {
        const Pad p = flow_pad(set.cts, set.cts_count, *route, m.cts);
        cts = p.pin;
        cts_af = p.af;
        if (!cts) {
            return false;
        }
    }
    if (flow & UART_HW_FLOWCTRL_RTS) {
        const Pad p = flow_pad(set.rts, set.rts_count, *route, m.rts);
        rts = p.pin;
        rts_af = p.af;
        if (!rts) {
            return false;
        }
    }
    return true;
}

void CH32RVSerialMode::start(CH32RVHardwareSerial &port)
{
    Mode &m = modes[port._number];
    const uint32_t base = port.registers();
    uint16_t ctlr3 = 0;
    uint16_t ie = CH32RV_USART_CTLR1_RXNEIE;

    if (port._rx_pin == port._tx_pin) {
        /* Single wire: the receiver joined to TX inside, the pad open drain
         * (WCH's EVT USART_HalfDuplex: AF open drain and a pull-up). X035 /
         * X033 pads have no open drain: there it stays push-pull, and the
         * line is driven high between frames. */
        ctlr3 |= CH32RV_USART_CTLR3_HDSEL;
        ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(port._tx_pin), (uint8_t)CH32RV_PIN_BIT(port._tx_pin),
                             CH32RV_GPIO_NO_OPEN_DRAIN ? CH32RV_GPIO_CFG_AF_PP_50M
                                                       : CH32RV_GPIO_CFG_AF_OD_50M);
    }

    m.cts_on = m.rts_on = 0;
    if (m.mode == UART_MODE_UART && m.flow) {
        uint8_t cts, rts, cts_af = 0, rts_af = 0;
        flowPads(port, m.flow, cts, rts, cts_af, rts_af);
        if (cts) {
            /* Pulled up: a CTS nobody drives holds what goes out. */
            pad_setup(cts, CH32RV_GPIO_CFG_IN_PULL, true, cts_af);
            ctlr3 |= CH32RV_USART_CTLR3_CTSE;
            m.cts_on = cts;
        }
        if (rts) {
            pad_setup(rts, CH32RV_GPIO_CFG_AF_PP_50M, false, rts_af);
            ctlr3 |= CH32RV_USART_CTLR3_RTSE;
            m.rts_on = rts;
        }
    }

    /* IrDA (normal mode): each 0 bit a pulse of 3/16 of a bit, a 1 nothing;
     * the line idles low. PSC must be 1 outside the low-power mode. */
    if (m.mode == UART_MODE_IRDA) {
        ctlr3 |= CH32RV_USART_CTLR3_IREN;
        CH32RV_USART_GPR(base) = (uint16_t)((CH32RV_USART_GPR(base) & 0xFF00u) | 1u);
    }

    m.de = 0;
    if (m.mode == UART_MODE_RS485_HALF_DUPLEX && m.rts) {
        const uint8_t p = (uint8_t)CH32RV_PIN_PORT(m.rts), b = (uint8_t)CH32RV_PIN_BIT(m.rts);
        ch32rv_gpio_clock_enable(p);
        ch32rv_gpio_clear(p, b);
        ch32rv_gpio_set_config(p, b, CH32RV_GPIO_CFG_OUT_PP_10M);
        m.de = m.rts;
    }

    if (m.on_receive && symbols(m)) {
        ie |= CH32RV_USART_CTLR1_IDLEIE;
    }
    m.held = 0;
    m.pending = 0;

    /* CTLR3 with the USART off (HDSEL, CTSE and RTSE are set up before UE,
     * as begin() does); what was enabled comes back with it. TE and RE go
     * off too: CH32V103 changes M (setAddress()'s 9-bit word) only in a
     * write after one that left both clear, and sets RWU only in a write
     * after the one that set UE (registers read on the bench, 2026-11-21:
     * M stayed 0 and the receiver never woke on its address). */
    const uint32_t irq = ch32rv_irq_save();
    const uint16_t ctlr1 = CH32RV_USART_CTLR1(base);
    CH32RV_USART_CTLR1(base) = (uint16_t)(ctlr1 & ~(CH32RV_USART_CTLR1_UE | CH32RV_USART_CTLR1_TE |
                                                    CH32RV_USART_CTLR1_RE));
    CH32RV_USART_CTLR3(base) = ctlr3;
    /* LIN: LINEN (a 13-bit break sent, breaks detected at 11 bits: LBDL),
     * one stop bit (the mode wants it); the bits written with the USART off */
    uint16_t ctlr2 = (uint16_t)(CH32RV_USART_CTLR2(base) & ~(CH32RV_USART_CTLR2_LINEN | CH32RV_USART_CTLR2_LBDL));
    if (m.mode == UART_MODE_LIN) {
        ctlr2 = (uint16_t)((ctlr2 & ~CH32RV_USART_CTLR2_STOP_MASK) | CH32RV_USART_CTLR2_LINEN | CH32RV_USART_CTLR2_LBDL);
    }
    CH32RV_USART_CTLR2(base) = ctlr2;
    uint16_t keep = (uint16_t)(ctlr1 & ~(CH32RV_USART_CTLR1_IDLEIE | CH32RV_USART_CTLR1_TCIE));
    if (address_ops) {
        keep = address_ops->format(port, base, keep);
    }
    const uint16_t word = (uint16_t)(keep | ie | CH32RV_USART_CTLR1_UE);
    CH32RV_USART_CTLR1(base) = (uint16_t)(word & ~CH32RV_USART_CTLR1_RWU);
    if (word & CH32RV_USART_CTLR1_RWU) {
        CH32RV_USART_CTLR1(base) = word;
    }
    /* A port on DMA (HardwareSerialDmaRx.cpp / Tx.cpp): its requests back,
     * no interrupt per received byte, and a transmit run in flight keeps its
     * transmit-complete interrupt. Interrupts still off: nothing saw the
     * moment between. */
    if (CH32RV_SERIAL_DMA_ON(port, RX)) {
        CH32RV_USART_CTLR3(base) |= CH32RV_USART_CTLR3_DMAR;
        CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_RXNEIE;
    }
    if (CH32RV_SERIAL_DMA_ON(port, TX)) {
        CH32RV_USART_CTLR3(base) |= CH32RV_USART_CTLR3_DMAT;
        CH32RV_USART_CTLR1(base) |= (uint16_t)(ctlr1 & CH32RV_USART_CTLR1_TCIE);
    }
    ch32rv_irq_restore(irq);
}

void CH32RVSerialMode::end(CH32RVHardwareSerial &port)
{
    Mode &m = modes[port._number];
    if (m.de) {
        de_write(m.de, false);        /* off the bus; the pad stays a low output */
    }
    if (m.cts_on) {
        pad_release(m.cts_on);
    }
    if (m.rts_on) {
        pad_release(m.rts_on);
    }
    m.cts_on = m.rts_on = 0;
    m.held = 0;
    m.pending = 0;
}

void CH32RVSerialMode::irq(CH32RVHardwareSerial &port, uint32_t base)
{
    constexpr uint32_t RX_FLAGS = CH32RV_USART_STATR_RXNE | CH32RV_USART_STATR_ORE | CH32RV_USART_STATR_NE |
                                  CH32RV_USART_STATR_FE | CH32RV_USART_STATR_PE;
    Mode &m = modes[port._number];
    uint32_t status = CH32RV_USART_STATR(base);
    const uint16_t ctlr1 = CH32RV_USART_CTLR1(base);
    if (m.held) {
        /* RTS: the byte waits in the data register, RTS high, until read()
         * makes room. Taking it here (for a transmit interrupt) would lose
         * it. */
        status &= ~(RX_FLAGS | CH32RV_USART_STATR_IDLE);
    }
    const bool idle = (status & CH32RV_USART_STATR_IDLE) && (ctlr1 & CH32RV_USART_CTLR1_IDLEIE);
    /* Receive by DMA: only a byte waiting for the DMA is not ours to take;
     * an error flag stays until the next byte's read and must not keep IDLE
     * set (the interrupt would come back at once). */
    const uint32_t taken = CH32RV_SERIAL_DMA_ON(port, RX) ? CH32RV_USART_STATR_RXNE : RX_FLAGS;
    if (idle && !(status & taken)) {
        /* IDLE alone is cleared by reading STATR, then DATAR. A byte that
         * came meanwhile is still taken (status read again). */
        status = CH32RV_USART_STATR(base);
        if (!(status & taken)) {
            (void)CH32RV_USART_DATAR(base);
            if (CH32RV_SERIAL_DMA_ON(port, RX)) {
                /* The read cleared the last byte's error flags too, before
                 * the DMA's reader saw them. */
                port._errors |= (uint8_t)(status & (CH32RV_USART_STATR_ORE | CH32RV_USART_STATR_NE |
                                                    CH32RV_USART_STATR_FE | CH32RV_USART_STATR_PE));
            }
        }
    }
    /* Receive by DMA: the DMA takes every frame (setAddress() refuses it). */
    if (!address_ops || CH32RV_SERIAL_DMA_ON(port, RX) || !address_ops->receive(port, base, status)) {
        port.serviceRx(base, status);
    }
    if (status & CH32RV_USART_STATR_RXNE) {
        m.pending &= (uint8_t)~P_IDLE;      /* a byte: the quiet starts again */
    }
    if (idle) {
        m.idle_at = micros();
        if (CH32RV_SERIAL_DMA_ON(port, RX)) {
            m.idle_pos = ch32rv_serial_dma_rx->position(port);
        }
        m.pending |= P_IDLE;
    }
    /* Receive by DMA: the count is the DMA's, read after loop() (deliver). */
    if ((m.rts_on || m.on_receive) && !CH32RV_SERIAL_DMA_ON(port, RX)) {
        const int count = port._rx.available();
        if (m.on_receive && count >= full_at(m)) {
            m.pending |= P_FULL;
        }
        if (m.rts_on && count >= hold_at(m)) {
            /* The next byte stays in the data register (RTS goes high at its
             * stop bit); IDLE could not be cleared without taking it. */
            CH32RV_USART_CTLR1(base) &= (uint16_t)~(CH32RV_USART_CTLR1_RXNEIE | CH32RV_USART_CTLR1_IDLEIE);
            m.held = 1;
        }
    }

    port.serviceTx(base, status);
    /* RS485: the last stop bit is out (TC, read now: a byte just written
     * cleared it) and nothing waits - the driver off the bus. */
    if ((ctlr1 & CH32RV_USART_CTLR1_TCIE) && (CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TC) &&
        port._tx.isEmpty() && !(CH32RV_SERIAL_DMA_ON(port, TX) && ch32rv_serial_dma_tx->sending(port))) {
        CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TCIE;
        if (m.de) {
            de_write(m.de, false);
        }
    }
}

void CH32RVSerialMode::read(CH32RVHardwareSerial &port)
{
    Mode &m = modes[port._number];
    if (m.held && port._rx.available() < hold_at(m)) {
        const uint32_t base = port.registers();
        const uint32_t irq = ch32rv_irq_save();
        m.held = 0;
        /* The byte kept in the data register comes in now, RTS low again. */
        CH32RV_USART_CTLR1(base) |= (uint16_t)(CH32RV_USART_CTLR1_RXNEIE |
                                              (m.on_receive && symbols(m) ? CH32RV_USART_CTLR1_IDLEIE : 0u));
        ch32rv_irq_restore(irq);
    }
}

void CH32RVSerialMode::write(CH32RVHardwareSerial &port)
{
    Mode &m = modes[port._number];
    if (!m.de) {
        return;
    }
    const uint32_t base = port.registers();
    /* The driver on before the byte is handed over, and the transmit-complete
     * interrupt off until it has been: a TC from the previous byte must not
     * drop DE under this one. */
    const uint32_t irq = ch32rv_irq_save();
    de_write(m.de, true);
    CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TCIE;
    ch32rv_irq_restore(irq);
}

void CH32RVSerialMode::written(CH32RVHardwareSerial &port)
{
    Mode &m = modes[port._number];
    if (!m.de) {
        return;
    }
    /* Handing the byte over (DATAR written after STATR read) cleared TC, so
     * the next TC is this byte's - or a later one's - end. */
    CH32RV_USART_CTLR1(port.registers()) |= CH32RV_USART_CTLR1_TCIE;
}

/* ------------------------------------------------- setPins()' side */
bool CH32RVSerialMode::setControlPins(CH32RVHardwareSerial &port, int cts, int rts)
{
    const uint8_t c = resolve(cts), r = resolve(rts);
    if ((cts >= 0 && !c) || (rts >= 0 && !r)) {
        return false;
    }
    Mode &m = modes[port._number];
    if (cts >= 0) {
        m.cts = c;
    }
    if (rts >= 0) {
        m.rts = r;
    }
    install();
    if (port._slot) {
        port.flush();
        end(port);
        start(port);
    }
    return true;
}

bool CH32RVSerialMode::setOneWire(CH32RVHardwareSerial &port, uint8_t index, const ch32rv_route_t &row,
                                  int cts, int rts)
{
    if ((cts >= 0 || rts >= 0) && !setControlPins(port, cts, rts)) {
        return false;
    }
    Mode &m = modes[port._number];
    m.one_wire = row;
    m.one_wire.pins[1] = row.pins[0];   /* RX is TX: start() sees one pad */
    install();
    port.useRoute(index, &m.one_wire);
    return true;
}

/* ------------------------------------------------- the receive events */
/* The delivery (HardwareSerialEvent.cpp attaches it, so that a sketch without
 * onReceive() links no delivery round). */
size_t CH32RVSerialMode::deliver(void)
{
    size_t ran = 0;
    for (Mode &m : modes) {
        CH32RVHardwareSerial *port = m.port;
        if (!port || !m.on_receive || !*port) {
            continue;
        }
        if (CH32RV_SERIAL_DMA_ON(*port, RX)) {
            /* No interrupt per byte: the count is read here, and bytes that
             * came after the IDLE make it wait for the next one. */
            const bool full = port->available() >= full_at(m);
            const uint32_t irq = ch32rv_irq_save();
            if (ch32rv_serial_dma_rx->position(*port) != m.idle_pos) {
                m.pending &= (uint8_t)~P_IDLE;
            }
            if (full) {
                m.pending |= P_FULL;
            }
            ch32rv_irq_restore(irq);
        }
        const uint8_t p = m.pending;
        uint8_t done = 0;
        bool fire = false;
        if (p & P_FULL) {
            done |= P_FULL;
            fire = !m.only_timeout || !symbols(m);
        }
        if (p & P_IDLE) {
            const uint8_t s = symbols(m);
            if (s <= 1u || (uint32_t)(micros() - m.idle_at) >= (uint32_t)(s - 1u) * frameMicros(*port)) {
                done |= P_IDLE;
                fire = true;
            }
        }
        if (done) {
            const uint32_t irq = ch32rv_irq_save();
            m.pending &= (uint8_t)~done;
            ch32rv_irq_restore(irq);
        }
        if (fire && port->available() > 0) {
            m.on_receive();
            ++ran;
        }
    }
    return ran;
}

void CH32RVSerialMode::setOnReceive(CH32RVHardwareSerial &port, const CH32RVCallback<void(void)> &function,
                                    bool onlyOnTimeout)
{
    Mode &m = modes[port._number];
    const uint32_t irq = ch32rv_irq_save();
    m.on_receive = function;
    m.only_timeout = onlyOnTimeout;
    m.pending = 0;
    m.port = &port;
    ch32rv_irq_restore(irq);
    install();
    if (port._slot) {
        start(port);                   /* IDLEIE on or off */
    }
}

/* One frame on the line, in microseconds: start, data, parity and stop bits
 * at the baud BRR makes of the USART's clock. */
uint32_t CH32RVSerialMode::frameMicros(CH32RVHardwareSerial &port)
{
    const uint16_t config = port._config;
    uint32_t bits = 1u + 4u + ((config & SERIAL_DATA_MASK) >> 8);       /* SERIAL_DATA_5 is 0x100 */
    const uint32_t parity = config & SERIAL_PARITY_MASK;
    bits += parity != SERIAL_PARITY_NONE && parity ? 1u : 0u;
    bits += address_ops && addressing(modes[port._number], config) ? 1u : 0u;   /* setAddress()'s mark */
    bits += (config & SERIAL_STOP_BIT_MASK) == SERIAL_STOP_BIT_2 ? 2u : 1u;
    const uint32_t fck = port.peripheral() == 1 ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
    const uint32_t brr = CH32RV_USART_BRR(port.registers());
    return (uint32_t)((uint64_t)bits * brr * 1000000u / fck);
}

/* ------------------------------------------------- setAddress()'s hooks */
namespace {

/* The mark one bit past the data (M for 8 data bits), the wake on an address
 * frame, the address, and muted from the start. Without setAddress() the word
 * is begin()'s again (M only for 8 bits and parity). */
uint16_t address_word(CH32RVHardwareSerial &port, uint32_t base, uint16_t ctlr1)
{
    const Mode &m = modes[port.number()];
    const uint16_t config = CH32RVSerialMode::config(port);
    ctlr1 &= (uint16_t)~(CH32RV_USART_CTLR1_WAKE | CH32RV_USART_CTLR1_RWU);
    if (has_parity(config)) {
        return ctlr1;
    }
    ctlr1 &= (uint16_t)~CH32RV_USART_CTLR1_M;
    if (addressing(m, config)) {
        ctlr1 |= (uint16_t)(CH32RV_USART_CTLR1_WAKE | CH32RV_USART_CTLR1_RWU |
                            (address_mark(config) == 0x100u ? CH32RV_USART_CTLR1_M : 0u));
        CH32RV_USART_CTLR2(base) = (uint16_t)((CH32RV_USART_CTLR2(base) & ~CH32RV_USART_CTLR2_ADD) | m.address);
    }
    return ctlr1;
}

/* The address frame that woke the receiver comes in as a byte with the mark
 * set: not data, so not kept. A frame in error is the plain path's. */
bool address_receive(CH32RVHardwareSerial &port, uint32_t base, uint32_t status)
{
    const uint16_t config = CH32RVSerialMode::config(port);
    if (!addressing(modes[port.number()], config) || !(status & CH32RV_USART_STATR_RXNE) ||
        (status & (CH32RV_USART_STATR_FE | CH32RV_USART_STATR_PE))) {
        return false;
    }
    const uint32_t data = CH32RV_USART_DATAR(base);
    CH32RVSerialMode::received(port, (uint8_t)(status & (CH32RV_USART_STATR_ORE | CH32RV_USART_STATR_NE)),
                               (data & address_mark(config)) ? -1 : (int)(uint8_t)data);
    return true;
}

const AddressOps address_hooks = {address_word, address_receive};

}  // namespace

uint16_t CH32RVSerialMode::config(CH32RVHardwareSerial &port)
{
    return port._config;
}

void CH32RVSerialMode::received(CH32RVHardwareSerial &port, uint8_t errors, int c)
{
    port._errors |= errors;
    if (c >= 0 && !port._rx.push((uint8_t)c)) {
        port._errors |= CH32RV_SERIAL_MARK_FULL;
    }
}

/* ------------------------------------------------- the API */
namespace arduino {

bool CH32RVHardwareSerial::setHwFlowCtrlMode(SerialHwFlowCtrl mode, uint8_t threshold)
{
    if ((unsigned)mode > UART_HW_FLOWCTRL_CTS_RTS) {
        return false;
    }
    Mode &m = modes[_number];
    if (m.mode != UART_MODE_UART && mode != UART_HW_FLOWCTRL_DISABLE) {
        return false;                  /* RS485: RTS is the driver enable */
    }
    if ((mode & UART_HW_FLOWCTRL_RTS) && CH32RV_SERIAL_DMA_ON(*this, RX)) {
        return false;                  /* the DMA empties the data register RTS follows */
    }
    uint8_t cts, rts, af;
    if (mode != UART_HW_FLOWCTRL_DISABLE && !CH32RVSerialMode::flowPads(*this, mode, cts, rts, af, af)) {
        return false;                  /* no such pad on this route */
    }
    install();
    if (_slot) {
        flush();
        CH32RVSerialMode::end(*this);  /* the old pads let go */
    }
    m.flow = (uint8_t)mode;
    m.threshold = threshold;
    if (_slot) {
        CH32RVSerialMode::start(*this);
    }
    return true;
}

bool CH32RVHardwareSerial::setMode(SerialMode mode)
{
    Mode &m = modes[_number];
    if (mode == UART_MODE_RS485_HALF_DUPLEX) {
        if (!m.rts) {
            return false;              /* setPins(-1, -1, -1, de) first */
        }
    } else if (mode != UART_MODE_UART && mode != UART_MODE_IRDA && mode != UART_MODE_LIN) {
        return false;                  /* the RS485 test modes: not here */
    }
    install();
    if (_slot) {
        flush();
        CH32RVSerialMode::end(*this);
    }
    if (m.mode == UART_MODE_RS485_HALF_DUPLEX && mode == UART_MODE_UART && m.rts) {
        pad_release(m.rts);            /* no longer the driver enable */
        m.de = 0;
    }
    m.mode = (uint8_t)mode;
    if (_slot) {
        CH32RVSerialMode::start(*this);
    }
    return true;
}

bool CH32RVHardwareSerial::setAddress(int address)
{
    if (address < -1 || address > (int)CH32RV_USART_CTLR2_ADD) {
        return false;
    }
    /* Open: its format and receive path now. Closed: begin()'s format
     * decides (start() leaves a port without room for the mark plain). */
    if (address >= 0 && _slot && (!address_format(_config) || CH32RV_SERIAL_DMA_ON(*this, RX))) {
        return false;
    }
    Mode &m = modes[_number];
    install();
    address_ops = &address_hooks;
    if (_slot) {
        flush();
        CH32RVSerialMode::end(*this);
    }
    m.addressed = address >= 0;
    m.address = address >= 0 ? (uint8_t)address : 0u;
    if (_slot) {
        CH32RVSerialMode::start(*this);
    }
    return true;
}

bool CH32RVHardwareSerial::writeAddress(uint8_t address)
{
    if (!_slot || address > CH32RV_USART_CTLR2_ADD || !address_ops || !addressing(modes[_number], _config)) {
        return false;
    }
    /* Past the ring (it holds bytes): what is in it goes first. */
    flush();
    const uint32_t base = registers();
    CH32RVSerialMode::write(*this);    /* RS485: the driver on */
    while ((CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TXE) == 0u) {
    }
    CH32RV_USART_DATAR(base) = (uint16_t)(address_mark(_config) | address);
    CH32RVSerialMode::written(*this);
    return true;
}

bool CH32RVHardwareSerial::mute(void)
{
    if (!_slot) {
        return false;
    }
    const uint32_t base = registers();
    const uint32_t irq = ch32rv_irq_save();
    CH32RV_USART_CTLR1(base) |= CH32RV_USART_CTLR1_RWU;
    ch32rv_irq_restore(irq);
    return true;
}

bool CH32RVHardwareSerial::setRxTimeout(uint8_t symbols)
{
    Mode &m = modes[_number];
    m.timeout = symbols;
    m.timeout_given = 1;
    if (_slot && m.on_receive) {
        CH32RVSerialMode::start(*this);
    }
    return true;
}

bool CH32RVHardwareSerial::setRxFIFOFull(uint8_t fifoBytes)
{
    if (!fifoBytes) {
        return false;
    }
    modes[_number].fifo_full = fifoBytes < CAPACITY ? fifoBytes : CAPACITY;
    return true;
}

}  // namespace arduino

#endif
