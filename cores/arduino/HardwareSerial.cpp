#include "HardwareSerial.h"

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_interrupt.h"
#include "internal/ch32rv_serial_irq.h"

using namespace arduino;

/* Which USARTs are in the table, and the board's choice for each (Disable /
 * AUTO / FIX): ch32rv_instances.h, through HardwareSerial.h. */
#define CH32RV_USART_DISABLE CH32RV_PERIPH_DISABLE
#define CH32RV_USART_AUTO    CH32RV_PERIPH_AUTO
#define CH32RV_USART_FIX     CH32RV_PERIPH_FIX

/* A variant that knows no AFIO field for a USART names no masks. */
#if defined(CH32RV_SERIAL1_CLKEN_ADDR) && !defined(CH32RV_SERIAL1_REMAP_MASK)
#define CH32RV_SERIAL1_REMAP_MASK 0u
#define CH32RV_SERIAL1_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL1_CLKEN_ADDR) && !defined(CH32RV_SERIAL1_REMAP2_MASK)
#define CH32RV_SERIAL1_REMAP2_MASK 0u
#define CH32RV_SERIAL1_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL2_CLKEN_ADDR) && !defined(CH32RV_SERIAL2_REMAP_MASK)
#define CH32RV_SERIAL2_REMAP_MASK 0u
#define CH32RV_SERIAL2_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL2_CLKEN_ADDR) && !defined(CH32RV_SERIAL2_REMAP2_MASK)
#define CH32RV_SERIAL2_REMAP2_MASK 0u
#define CH32RV_SERIAL2_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL3_CLKEN_ADDR) && !defined(CH32RV_SERIAL3_REMAP_MASK)
#define CH32RV_SERIAL3_REMAP_MASK 0u
#define CH32RV_SERIAL3_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL3_CLKEN_ADDR) && !defined(CH32RV_SERIAL3_REMAP2_MASK)
#define CH32RV_SERIAL3_REMAP2_MASK 0u
#define CH32RV_SERIAL3_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL4_CLKEN_ADDR) && !defined(CH32RV_SERIAL4_REMAP_MASK)
#define CH32RV_SERIAL4_REMAP_MASK 0u
#define CH32RV_SERIAL4_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL4_CLKEN_ADDR) && !defined(CH32RV_SERIAL4_REMAP2_MASK)
#define CH32RV_SERIAL4_REMAP2_MASK 0u
#define CH32RV_SERIAL4_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL5_CLKEN_ADDR) && !defined(CH32RV_SERIAL5_REMAP_MASK)
#define CH32RV_SERIAL5_REMAP_MASK 0u
#define CH32RV_SERIAL5_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL5_CLKEN_ADDR) && !defined(CH32RV_SERIAL5_REMAP2_MASK)
#define CH32RV_SERIAL5_REMAP2_MASK 0u
#define CH32RV_SERIAL5_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL6_CLKEN_ADDR) && !defined(CH32RV_SERIAL6_REMAP_MASK)
#define CH32RV_SERIAL6_REMAP_MASK 0u
#define CH32RV_SERIAL6_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL6_CLKEN_ADDR) && !defined(CH32RV_SERIAL6_REMAP2_MASK)
#define CH32RV_SERIAL6_REMAP2_MASK 0u
#define CH32RV_SERIAL6_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL7_CLKEN_ADDR) && !defined(CH32RV_SERIAL7_REMAP_MASK)
#define CH32RV_SERIAL7_REMAP_MASK 0u
#define CH32RV_SERIAL7_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL7_CLKEN_ADDR) && !defined(CH32RV_SERIAL7_REMAP2_MASK)
#define CH32RV_SERIAL7_REMAP2_MASK 0u
#define CH32RV_SERIAL7_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL8_CLKEN_ADDR) && !defined(CH32RV_SERIAL8_REMAP_MASK)
#define CH32RV_SERIAL8_REMAP_MASK 0u
#define CH32RV_SERIAL8_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL8_CLKEN_ADDR) && !defined(CH32RV_SERIAL8_REMAP2_MASK)
#define CH32RV_SERIAL8_REMAP2_MASK 0u
#define CH32RV_SERIAL8_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL9_CLKEN_ADDR) && !defined(CH32RV_SERIAL9_REMAP_MASK)
#define CH32RV_SERIAL9_REMAP_MASK 0u
#define CH32RV_SERIAL9_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL9_CLKEN_ADDR) && !defined(CH32RV_SERIAL9_REMAP2_MASK)
#define CH32RV_SERIAL9_REMAP2_MASK 0u
#define CH32RV_SERIAL9_REMAP2_VAL  0u
#endif
#if defined(CH32RV_SERIAL10_CLKEN_ADDR) && !defined(CH32RV_SERIAL10_REMAP_MASK)
#define CH32RV_SERIAL10_REMAP_MASK 0u
#define CH32RV_SERIAL10_REMAP_VAL  0u
#endif
#if defined(CH32RV_SERIAL10_CLKEN_ADDR) && !defined(CH32RV_SERIAL10_REMAP2_MASK)
#define CH32RV_SERIAL10_REMAP2_MASK 0u
#define CH32RV_SERIAL10_REMAP2_VAL  0u
#endif

/* ------------------------------------------------------- the USART table */
namespace {

struct UsartDesc {
    uint32_t base;
    uint32_t clken_addr;
    uint32_t clken_mask;
    uint32_t remap_mask;     /* PCFR1 field, 0 = none */
    uint32_t remap2_mask;    /* PCFR2 half, where the field spans it */
    ch32rv_route_t def;      /* the route a begin() without pins uses */
    uint8_t irqn;
    uint8_t number;          /* n of USARTn */
};

/* Every route a USART can take, for setPins() / setRoute() / begin() with pins
 * only. Kept apart from the table above so that a sketch which never names
 * pins does not link it. */
struct RouteSet {
    const ch32rv_route_t *rows;
    uint8_t count;
};

/* AUTO: the variant's routes, or its one default route where the variant has
 * no AFIO field for the USART. FIX: the board's route alone. */
#define CH32RV_USART_ROWS(n)                                                        \
    static const ch32rv_route_t usart##n##_routes[] = CH32RV_USART_ROUTES_##n;

#define CH32RV_USART_ONE_ROUTE(n)                                                   \
    { { 0, { CH32RV_SERIAL##n##_TX, CH32RV_SERIAL##n##_RX, CH32RV_ROUTE_NO_PIN },   \
        CH32RV_SERIAL##n##_REMAP_VAL, CH32RV_SERIAL##n##_REMAP2_VAL } }
/* A USART the variant lists no route for (none it can name for ANY): a row
 * that matches no pin. */
#define CH32RV_USART_NO_ROUTE                                                       \
    { { 0, { CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN }, 0u, 0u } }
#define CH32RV_USART_FIX_ROUTE(n)                                                   \
    { { 0, { CH32RV_USART##n##_FIX_TX, CH32RV_USART##n##_FIX_RX, CH32RV_ROUTE_NO_PIN }, \
        CH32RV_USART##n##_FIX_V1, CH32RV_USART##n##_FIX_V2 } }

#if CH32RV_USART_ON_1
#if CH32RV_USART1_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_1 CH32RV_USART_FIX_ROUTE(1)
#elif defined(CH32RV_SERIAL1_ROUTES)
#define CH32RV_USART_ROUTES_1 CH32RV_SERIAL1_ROUTES
#elif defined(CH32RV_SERIAL1_TX)
#define CH32RV_USART_ROUTES_1 CH32RV_USART_ONE_ROUTE(1)
#else
#define CH32RV_USART_ROUTES_1 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(1)
#define CH32RV_USART_HAS_1 1
#endif
#if CH32RV_USART_ON_2
#if CH32RV_USART2_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_2 CH32RV_USART_FIX_ROUTE(2)
#elif defined(CH32RV_SERIAL2_ROUTES)
#define CH32RV_USART_ROUTES_2 CH32RV_SERIAL2_ROUTES
#elif defined(CH32RV_SERIAL2_TX)
#define CH32RV_USART_ROUTES_2 CH32RV_USART_ONE_ROUTE(2)
#else
#define CH32RV_USART_ROUTES_2 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(2)
#define CH32RV_USART_HAS_2 1
#endif
#if CH32RV_USART_ON_3
#if CH32RV_USART3_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_3 CH32RV_USART_FIX_ROUTE(3)
#elif defined(CH32RV_SERIAL3_ROUTES)
#define CH32RV_USART_ROUTES_3 CH32RV_SERIAL3_ROUTES
#elif defined(CH32RV_SERIAL3_TX)
#define CH32RV_USART_ROUTES_3 CH32RV_USART_ONE_ROUTE(3)
#else
#define CH32RV_USART_ROUTES_3 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(3)
#define CH32RV_USART_HAS_3 1
#endif
#if CH32RV_USART_ON_4
#if CH32RV_USART4_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_4 CH32RV_USART_FIX_ROUTE(4)
#elif defined(CH32RV_SERIAL4_ROUTES)
#define CH32RV_USART_ROUTES_4 CH32RV_SERIAL4_ROUTES
#elif defined(CH32RV_SERIAL4_TX)
#define CH32RV_USART_ROUTES_4 CH32RV_USART_ONE_ROUTE(4)
#else
#define CH32RV_USART_ROUTES_4 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(4)
#define CH32RV_USART_HAS_4 1
#endif
#if CH32RV_USART_ON_5
#if CH32RV_USART5_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_5 CH32RV_USART_FIX_ROUTE(5)
#elif defined(CH32RV_SERIAL5_ROUTES)
#define CH32RV_USART_ROUTES_5 CH32RV_SERIAL5_ROUTES
#elif defined(CH32RV_SERIAL5_TX)
#define CH32RV_USART_ROUTES_5 CH32RV_USART_ONE_ROUTE(5)
#else
#define CH32RV_USART_ROUTES_5 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(5)
#define CH32RV_USART_HAS_5 1
#endif
#if CH32RV_USART_ON_6
#if CH32RV_USART6_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_6 CH32RV_USART_FIX_ROUTE(6)
#elif defined(CH32RV_SERIAL6_ROUTES)
#define CH32RV_USART_ROUTES_6 CH32RV_SERIAL6_ROUTES
#elif defined(CH32RV_SERIAL6_TX)
#define CH32RV_USART_ROUTES_6 CH32RV_USART_ONE_ROUTE(6)
#else
#define CH32RV_USART_ROUTES_6 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(6)
#define CH32RV_USART_HAS_6 1
#endif
#if CH32RV_USART_ON_7
#if CH32RV_USART7_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_7 CH32RV_USART_FIX_ROUTE(7)
#elif defined(CH32RV_SERIAL7_ROUTES)
#define CH32RV_USART_ROUTES_7 CH32RV_SERIAL7_ROUTES
#elif defined(CH32RV_SERIAL7_TX)
#define CH32RV_USART_ROUTES_7 CH32RV_USART_ONE_ROUTE(7)
#else
#define CH32RV_USART_ROUTES_7 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(7)
#define CH32RV_USART_HAS_7 1
#endif
#if CH32RV_USART_ON_8
#if CH32RV_USART8_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_8 CH32RV_USART_FIX_ROUTE(8)
#elif defined(CH32RV_SERIAL8_ROUTES)
#define CH32RV_USART_ROUTES_8 CH32RV_SERIAL8_ROUTES
#elif defined(CH32RV_SERIAL8_TX)
#define CH32RV_USART_ROUTES_8 CH32RV_USART_ONE_ROUTE(8)
#else
#define CH32RV_USART_ROUTES_8 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(8)
#define CH32RV_USART_HAS_8 1
#endif
#if CH32RV_USART_ON_9
#if CH32RV_USART9_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_9 CH32RV_USART_FIX_ROUTE(9)
#elif defined(CH32RV_SERIAL9_ROUTES)
#define CH32RV_USART_ROUTES_9 CH32RV_SERIAL9_ROUTES
#elif defined(CH32RV_SERIAL9_TX)
#define CH32RV_USART_ROUTES_9 CH32RV_USART_ONE_ROUTE(9)
#else
#define CH32RV_USART_ROUTES_9 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(9)
#define CH32RV_USART_HAS_9 1
#endif
#if CH32RV_USART_ON_10
#if CH32RV_USART10_MODE == CH32RV_USART_FIX
#define CH32RV_USART_ROUTES_10 CH32RV_USART_FIX_ROUTE(10)
#elif defined(CH32RV_SERIAL10_ROUTES)
#define CH32RV_USART_ROUTES_10 CH32RV_SERIAL10_ROUTES
#elif defined(CH32RV_SERIAL10_TX)
#define CH32RV_USART_ROUTES_10 CH32RV_USART_ONE_ROUTE(10)
#else
#define CH32RV_USART_ROUTES_10 CH32RV_USART_NO_ROUTE
#endif
CH32RV_USART_ROWS(10)
#define CH32RV_USART_HAS_10 1
#endif

/* A FIX USART's default pins are its fixed pins; AUTO's are the variant's, and
 * a USART the variant has no default route for has none: begin() without pins
 * fails on it. */
#define CH32RV_USART_NO_DEF CH32RV_ROUTE_NO_PIN, CH32RV_ROUTE_NO_PIN, 0u, 0u
#define CH32RV_USART_ROUTE4(tx, rx, v1, v2) { 0, { tx, rx, CH32RV_ROUTE_NO_PIN }, v1, v2 }
#define CH32RV_USART_ROUTE(...) CH32RV_USART_ROUTE4(__VA_ARGS__)
#if CH32RV_USART1_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_1 CH32RV_USART1_FIX_TX, CH32RV_USART1_FIX_RX, CH32RV_USART1_FIX_V1, CH32RV_USART1_FIX_V2
#elif defined(CH32RV_SERIAL1_TX)
#define CH32RV_USART_DEF_1 CH32RV_SERIAL1_TX, CH32RV_SERIAL1_RX, CH32RV_SERIAL1_REMAP_VAL, CH32RV_SERIAL1_REMAP2_VAL
#else
#define CH32RV_USART_DEF_1 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART2_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_2 CH32RV_USART2_FIX_TX, CH32RV_USART2_FIX_RX, CH32RV_USART2_FIX_V1, CH32RV_USART2_FIX_V2
#elif defined(CH32RV_SERIAL2_TX)
#define CH32RV_USART_DEF_2 CH32RV_SERIAL2_TX, CH32RV_SERIAL2_RX, CH32RV_SERIAL2_REMAP_VAL, CH32RV_SERIAL2_REMAP2_VAL
#else
#define CH32RV_USART_DEF_2 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART3_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_3 CH32RV_USART3_FIX_TX, CH32RV_USART3_FIX_RX, CH32RV_USART3_FIX_V1, CH32RV_USART3_FIX_V2
#elif defined(CH32RV_SERIAL3_TX)
#define CH32RV_USART_DEF_3 CH32RV_SERIAL3_TX, CH32RV_SERIAL3_RX, CH32RV_SERIAL3_REMAP_VAL, CH32RV_SERIAL3_REMAP2_VAL
#else
#define CH32RV_USART_DEF_3 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART4_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_4 CH32RV_USART4_FIX_TX, CH32RV_USART4_FIX_RX, CH32RV_USART4_FIX_V1, CH32RV_USART4_FIX_V2
#elif defined(CH32RV_SERIAL4_TX)
#define CH32RV_USART_DEF_4 CH32RV_SERIAL4_TX, CH32RV_SERIAL4_RX, CH32RV_SERIAL4_REMAP_VAL, CH32RV_SERIAL4_REMAP2_VAL
#else
#define CH32RV_USART_DEF_4 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART5_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_5 CH32RV_USART5_FIX_TX, CH32RV_USART5_FIX_RX, CH32RV_USART5_FIX_V1, CH32RV_USART5_FIX_V2
#elif defined(CH32RV_SERIAL5_TX)
#define CH32RV_USART_DEF_5 CH32RV_SERIAL5_TX, CH32RV_SERIAL5_RX, CH32RV_SERIAL5_REMAP_VAL, CH32RV_SERIAL5_REMAP2_VAL
#else
#define CH32RV_USART_DEF_5 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART6_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_6 CH32RV_USART6_FIX_TX, CH32RV_USART6_FIX_RX, CH32RV_USART6_FIX_V1, CH32RV_USART6_FIX_V2
#elif defined(CH32RV_SERIAL6_TX)
#define CH32RV_USART_DEF_6 CH32RV_SERIAL6_TX, CH32RV_SERIAL6_RX, CH32RV_SERIAL6_REMAP_VAL, CH32RV_SERIAL6_REMAP2_VAL
#else
#define CH32RV_USART_DEF_6 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART7_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_7 CH32RV_USART7_FIX_TX, CH32RV_USART7_FIX_RX, CH32RV_USART7_FIX_V1, CH32RV_USART7_FIX_V2
#elif defined(CH32RV_SERIAL7_TX)
#define CH32RV_USART_DEF_7 CH32RV_SERIAL7_TX, CH32RV_SERIAL7_RX, CH32RV_SERIAL7_REMAP_VAL, CH32RV_SERIAL7_REMAP2_VAL
#else
#define CH32RV_USART_DEF_7 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART8_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_8 CH32RV_USART8_FIX_TX, CH32RV_USART8_FIX_RX, CH32RV_USART8_FIX_V1, CH32RV_USART8_FIX_V2
#elif defined(CH32RV_SERIAL8_TX)
#define CH32RV_USART_DEF_8 CH32RV_SERIAL8_TX, CH32RV_SERIAL8_RX, CH32RV_SERIAL8_REMAP_VAL, CH32RV_SERIAL8_REMAP2_VAL
#else
#define CH32RV_USART_DEF_8 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART9_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_9 CH32RV_USART9_FIX_TX, CH32RV_USART9_FIX_RX, CH32RV_USART9_FIX_V1, CH32RV_USART9_FIX_V2
#elif defined(CH32RV_SERIAL9_TX)
#define CH32RV_USART_DEF_9 CH32RV_SERIAL9_TX, CH32RV_SERIAL9_RX, CH32RV_SERIAL9_REMAP_VAL, CH32RV_SERIAL9_REMAP2_VAL
#else
#define CH32RV_USART_DEF_9 CH32RV_USART_NO_DEF
#endif
#if CH32RV_USART10_MODE == CH32RV_USART_FIX
#define CH32RV_USART_DEF_10 CH32RV_USART10_FIX_TX, CH32RV_USART10_FIX_RX, CH32RV_USART10_FIX_V1, CH32RV_USART10_FIX_V2
#elif defined(CH32RV_SERIAL10_TX)
#define CH32RV_USART_DEF_10 CH32RV_SERIAL10_TX, CH32RV_SERIAL10_RX, CH32RV_SERIAL10_REMAP_VAL, CH32RV_SERIAL10_REMAP2_VAL
#else
#define CH32RV_USART_DEF_10 CH32RV_USART_NO_DEF
#endif
#define CH32RV_USART_DESC(n, base)                                                  \
    { base, CH32RV_SERIAL##n##_CLKEN_ADDR, CH32RV_SERIAL##n##_CLKEN_MASK,             \
      CH32RV_SERIAL##n##_REMAP_MASK, CH32RV_SERIAL##n##_REMAP2_MASK,                  \
      CH32RV_USART_ROUTE(CH32RV_USART_DEF_##n), CH32RV_SERIAL##n##_IRQ, n },
#define CH32RV_USART_SET(n) \
    { usart##n##_routes, (uint8_t)(sizeof usart##n##_routes / sizeof usart##n##_routes[0]) },

#if defined(CH32RV_USART_HAS_1) || defined(CH32RV_USART_HAS_2) || defined(CH32RV_USART_HAS_3) || \
    defined(CH32RV_USART_HAS_4) || defined(CH32RV_USART_HAS_5) || defined(CH32RV_USART_HAS_6) || \
    defined(CH32RV_USART_HAS_7) || defined(CH32RV_USART_HAS_8) || defined(CH32RV_USART_HAS_9) || \
    defined(CH32RV_USART_HAS_10)
const UsartDesc usarts[] = {
#ifdef CH32RV_USART_HAS_1
    CH32RV_USART_DESC(1, CH32RV_USART1_BASE)
#endif
#ifdef CH32RV_USART_HAS_2
    CH32RV_USART_DESC(2, CH32RV_USART2_BASE)
#endif
#ifdef CH32RV_USART_HAS_3
    CH32RV_USART_DESC(3, CH32RV_USART3_BASE)
#endif
#ifdef CH32RV_USART_HAS_4
    CH32RV_USART_DESC(4, CH32RV_USART4_BASE)
#endif
#ifdef CH32RV_USART_HAS_5
    CH32RV_USART_DESC(5, CH32RV_USART5_BASE)
#endif
#ifdef CH32RV_USART_HAS_6
    CH32RV_USART_DESC(6, CH32RV_USART6_BASE)
#endif
#ifdef CH32RV_USART_HAS_7
    CH32RV_USART_DESC(7, CH32RV_USART7_BASE)
#endif
#ifdef CH32RV_USART_HAS_8
    CH32RV_USART_DESC(8, CH32RV_USART8_BASE)
#endif
#ifdef CH32RV_USART_HAS_9
    CH32RV_USART_DESC(9, CH32RV_USART9_BASE)
#endif
#ifdef CH32RV_USART_HAS_10
    CH32RV_USART_DESC(10, CH32RV_USART10_BASE)
#endif
};
constexpr uint8_t USART_COUNT = (uint8_t)(sizeof usarts / sizeof usarts[0]);
static_assert(USART_COUNT == CH32RV_SERIAL_COUNT, "base() and _base follow CH32RV_SERIAL_COUNT");
const RouteSet route_sets[] = {
#ifdef CH32RV_USART_HAS_1
    CH32RV_USART_SET(1)
#endif
#ifdef CH32RV_USART_HAS_2
    CH32RV_USART_SET(2)
#endif
#ifdef CH32RV_USART_HAS_3
    CH32RV_USART_SET(3)
#endif
#ifdef CH32RV_USART_HAS_4
    CH32RV_USART_SET(4)
#endif
#ifdef CH32RV_USART_HAS_5
    CH32RV_USART_SET(5)
#endif
#ifdef CH32RV_USART_HAS_6
    CH32RV_USART_SET(6)
#endif
#ifdef CH32RV_USART_HAS_7
    CH32RV_USART_SET(7)
#endif
#ifdef CH32RV_USART_HAS_8
    CH32RV_USART_SET(8)
#endif
#ifdef CH32RV_USART_HAS_9
    CH32RV_USART_SET(9)
#endif
#ifdef CH32RV_USART_HAS_10
    CH32RV_USART_SET(10)
#endif
};
#else
/* Every USART is Disable on this board: ports exist, begin() never opens one. */
const UsartDesc usarts[1] = {};
const RouteSet route_sets[1] = {};
constexpr uint8_t USART_COUNT = 0;
#endif

/* Which port holds each USART. The interrupt handler reads it. */
CH32RVHardwareSerial *owner[USART_COUNT > 0 ? USART_COUNT : 1];

/* The table index behind an open port's _slot. A series with one USART
 * (V003, V002/V004) has only index 0, and saying so lets the compiler turn
 * every table read into a constant - which is what keeps the smallest parts
 * as cheap as a port with its USART built in. */
inline uint8_t index_of(uint8_t slot)
{
    return USART_COUNT == 1 ? 0 : (uint8_t)(slot - 1);
}

/* Hand a pad back as a floating input. Leaving the old TX configured as an
 * alternate-function output would keep it driving after the port moved away
 * from it. */
void release_pin(uint8_t pin)
{
    ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pin), (uint8_t)CH32RV_PIN_BIT(pin),
                         CH32RV_GPIO_CFG_IN_FLOAT);
    /* And its alternate function back to the reset value, where pads choose
     * it one by one: a released RX left on the USART's function still fed
     * the USART, and won over the new RX (V205 2026-11-13: Serial1 on PA6 /
     * PA7, then PA6 / PA10, received nothing on PA10; USART5's PB5 then PB3
     * the same). */
    ch32rv_pin_set_af(pin, 0);
}

/* A free USART with a route on these pins: (index, row), or index -1. The
 * port's own row (`prefer`) is tried first, so where two USARTs share the pins
 * the name keeps the one it would get without pins. */
struct Found {
    int index;
    const ch32rv_route_t *row;
};

Found find_route(uint8_t prefer, uint8_t tx, uint8_t rx, const CH32RVHardwareSerial *self)
{
    Found first = {-1, nullptr};
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        if (owner[i] && owner[i] != self) {
            continue;
        }
        const RouteSet &set = route_sets[i];
        for (uint8_t r = 0; r < set.count; r++) {
            /* rx CH32RV_ROUTE_NO_PIN: any RX (single wire, TX alone). */
            if (set.rows[r].pins[0] == tx && (rx == CH32RV_ROUTE_NO_PIN || set.rows[r].pins[1] == rx)) {
                if (i == prefer) {
                    return {(int)i, &set.rows[r]};
                }
                if (first.index < 0) {
                    first = {(int)i, &set.rows[r]};
                }
            }
        }
    }
    return first;
}

}  // namespace

/* ------------------------------------------------------- the interrupt */
/* One handler for every enabled USART: each USARTn_IRQHandler below is an
 * alias of it, and mcause says which one fired. A Disable USART gets no alias,
 * so its vector stays on the default handler. The body is inlined here, once:
 * called as a function, the interrupt prologue would save all sixteen
 * caller-saved registers around the call (2026-09-29: an X035 at 8 MHz lost
 * bytes below about 200 CPU cycles a byte). Measured on V2 (V003), V3A (V103,
 * jump-instruction vector table) and V4 (X035): wch-protocols E172. */
__attribute__((always_inline)) inline uint32_t CH32RVHardwareSerial::base(void) const
{
#if CH32RV_SERIAL_COUNT > 1
    return _base;
#else
    return usarts[0].base;
#endif
}

__attribute__((always_inline)) inline void CH32RVHardwareSerial::irq(void)
{
    const uint32_t base = this->base();
    /* A mode beyond plain UART (flow control, RS485, single wire, receive
     * events) has an interrupt of its own. Nothing ever sets the pointer in a
     * sketch that uses none of them, and the link-time optimiser then drops
     * the test and the call: the handler stays the one below. */
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->irq(*this, base);
    } else {
        const uint16_t status = CH32RV_USART_STATR(base);
        serviceRx(base, status);       /* internal/ch32rv_serial_irq.h */
        serviceTx(base, status);
    }
    /* A DMA transmit's run ended (transmit complete): the next one. After the
     * modes', whose RS485 driver enable looks at the same TC first. Folds
     * away as the modes' pointer does. */
    if (CH32RV_SERIAL_DMA_ON(*this, TX)) {
        ch32rv_serial_dma_tx->irq(*this, base);
    }
}

extern "C" CH32RV_INTERRUPT void ch32rv_usart_irq(void)
{
    if (USART_COUNT == 1) {          /* one vector, so no need to ask which */
        if (owner[0]) {
            owner[0]->irq();
        }
        return;
    }
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    const uint8_t irqn = (uint8_t)(cause & 0xFFFu);
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        if (usarts[i].irqn == irqn) {
            if (owner[i]) {
                owner[i]->irq();
            }
            return;
        }
    }
}
#define CH32RV_USART_ALIAS(n) \
    extern "C" void CH32RV_SERIAL##n##_HANDLER(void) __attribute__((alias("ch32rv_usart_irq")));
#ifdef CH32RV_USART_HAS_1
CH32RV_USART_ALIAS(1)
#endif
#ifdef CH32RV_USART_HAS_2
CH32RV_USART_ALIAS(2)
#endif
#ifdef CH32RV_USART_HAS_3
CH32RV_USART_ALIAS(3)
#endif
#ifdef CH32RV_USART_HAS_4
CH32RV_USART_ALIAS(4)
#endif
#ifdef CH32RV_USART_HAS_5
CH32RV_USART_ALIAS(5)
#endif
#ifdef CH32RV_USART_HAS_6
CH32RV_USART_ALIAS(6)
#endif
#ifdef CH32RV_USART_HAS_7
CH32RV_USART_ALIAS(7)
#endif
#ifdef CH32RV_USART_HAS_8
CH32RV_USART_ALIAS(8)
#endif
#ifdef CH32RV_USART_HAS_9
CH32RV_USART_ALIAS(9)
#endif
#ifdef CH32RV_USART_HAS_10
CH32RV_USART_ALIAS(10)
#endif

/* ------------------------------------------------------- binding */
uint8_t CH32RVHardwareSerial::peripheral(void) const
{
    return _slot ? usarts[index_of(_slot)].number : 0;
}

__attribute__((always_inline)) inline void CH32RVHardwareSerial::start(void)
{
    const UsartDesc &d = usarts[index_of(_slot)];
    const uint8_t tx_port = (uint8_t)CH32RV_PIN_PORT(_tx_pin);
    const uint8_t rx_port = (uint8_t)CH32RV_PIN_PORT(_rx_pin);
    ch32rv_gpio_clock_enable(tx_port);
    ch32rv_gpio_clock_enable(rx_port);
    ch32rv_gpio_set_config(tx_port, (uint8_t)CH32RV_PIN_BIT(_tx_pin),
                         CH32RV_GPIO_CFG_AF_PP_50M);
    /* Pull the RX line up so an unconnected input idles as a mark instead of
     * generating framing errors. */
    ch32rv_gpio_set_config(rx_port, (uint8_t)CH32RV_PIN_BIT(_rx_pin),
                         CH32RV_GPIO_CFG_IN_PULL);
    ch32rv_gpio_set(rx_port, (uint8_t)CH32RV_PIN_BIT(_rx_pin));

    /* BRR holds USARTDIV * 16, which is exactly the rounded fck / baud.
     * USART1 hangs off PCLK2 and the others off PCLK1: the same clock unless
     * the CPU Clock entry halves APB2 (Arduino.h). */
#if CH32RV_PCLK1_HZ == CH32RV_PCLK2_HZ
    CH32RV_USART_BRR(d.base) = _brr;
#else
    const uint32_t fck = d.number == 1 ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
    CH32RV_USART_BRR(d.base) = (uint16_t)((fck + _baud / 2) / _baud);
#endif

    uint16_t ctlr1 = CH32RV_USART_CTLR1_TE | CH32RV_USART_CTLR1_RE |
                     CH32RV_USART_CTLR1_RXNEIE;
    uint16_t ctlr2 = 0;
    switch (_config & SERIAL_PARITY_MASK) {
    case SERIAL_PARITY_EVEN: ctlr1 |= CH32RV_USART_CTLR1_PCE; break;
    case SERIAL_PARITY_ODD:  ctlr1 |= CH32RV_USART_CTLR1_PCE | CH32RV_USART_CTLR1_PS; break;
    default: break;
    }
    /* The parity bit occupies the ninth position, so 8 data bits with parity
     * means a 9-bit word. */
    if ((_config & SERIAL_DATA_MASK) == SERIAL_DATA_8 &&
        (ctlr1 & CH32RV_USART_CTLR1_PCE)) {
        ctlr1 |= CH32RV_USART_CTLR1_M;
    }
    /* 7 data bits with parity are an 8-bit word: its top bit is the parity
     * (until 2026-11-05 read() returned it - 7E1 / 7O1 read bytes with bit 7
     * set by the parity, measured on CH32X035 with the probe's UART). */
    _rx_mask = ((_config & SERIAL_DATA_MASK) == SERIAL_DATA_7 &&
                (ctlr1 & CH32RV_USART_CTLR1_PCE)) ? 0x7Fu : 0xFFu;
    switch (_config & SERIAL_STOP_BIT_MASK) {
    case SERIAL_STOP_BIT_2:   ctlr2 |= CH32RV_USART_CTLR2_STOP_2; break;
    case SERIAL_STOP_BIT_1_5: ctlr2 |= CH32RV_USART_CTLR2_STOP_1P5; break;
    default: break;
    }
    CH32RV_USART_CTLR2(d.base) = ctlr2;
    CH32RV_USART_CTLR3(d.base) = 0;
    CH32RV_USART_CTLR1(d.base) = ctlr1 | CH32RV_USART_CTLR1_UE;
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->start(*this);   /* the mode's bits and pads */
    }
    /* setRxBufferSize() / setTxBufferSize(): their DMA channels, after the
     * modes (RTS flow control keeps the receive off DMA). */
    if (ch32rv_serial_dma_rx) {
        ch32rv_serial_dma_rx->start(*this);
    }
    if (ch32rv_serial_dma_tx) {
        ch32rv_serial_dma_tx->start(*this);
    }
    ch32rv_irq_enable(d.irqn);
}

/* Take USART `index` on `route` and program it at the remembered baud and
 * framing. Fails when another port holds that USART. */
__attribute__((always_inline)) inline bool CH32RVHardwareSerial::bind(uint8_t index,
                                                                    const ch32rv_route_t &route)
{
    if (USART_COUNT == 1) {
        index = 0;             /* the only USART: lets the table fold to constants */
    }
    if (owner[index] && owner[index] != this) {
        return false;
    }
    const UsartDesc &d = usarts[index];
    owner[index] = this;
    _slot = (uint8_t)(index + 1);
#if CH32RV_SERIAL_COUNT > 1
    _base = d.base;
#endif
    _tx_pin = route.pins[0];
    _rx_pin = route.pins[1];

    ch32rv_clock_enable_at(d.clken_addr, d.clken_mask);
    ch32rv_clock_enable(AFIO);
    /* Written every time, including for the default route, so begin() does
     * not depend on what the field already held. The selector is not always one
     * field in one register: on L103/M103 and V20x/V30x it spans PCFR1 and
     * PCFR2, and writing only PCFR1 selects a different route with nothing to
     * say so. */
    if (d.remap_mask) {
        CH32RV_AFIO_PCFR1 = (CH32RV_AFIO_PCFR1 & ~d.remap_mask) | route.value;
    }
    if (d.remap2_mask) {
        CH32RV_AFIO_PCFR2 = (CH32RV_AFIO_PCFR2 & ~d.remap2_mask) | route.value2;
    }
    ch32rv_pin_set_af(route.pins[0], route.value);
    ch32rv_pin_set_af(route.pins[1], route.value);
    start();
    return true;
}

void CH32RVHardwareSerial::begin(unsigned long baudrate, uint16_t config)
{
    if (_slot) {
        end();
    }
    /* Serial (row 0, SERIAL_PORT_MONITOR) is where printf() goes, from here on,
     * unless the sketch chose otherwise (ch32rv_stdout.cpp). */
    if (_number == 0 && ch32rv_stdout_offer) {
        ch32rv_stdout_offer(this);
    }
    /* BRR holds USARTDIV * 16 in 16 bits, and below 16 (USARTDIV < 1) the
     * USART does not refuse it: it runs at fck / 16 whatever was asked
     * (measured on X035 and V003, 2026-09-29: 921600 at 8 MHz came out at
     * 500 kbaud). Above 0xFFFF the value would be cut to 16 bits. Either way
     * the port would talk at a rate nobody asked for, so an unreachable baud
     * leaves it closed. The range is fck / 65535 .. fck / 16 (48 MHz: 733 baud
     * .. 3 Mbaud). */
#if CH32RV_PCLK1_HZ == CH32RV_PCLK2_HZ
    const unsigned long brr = baudrate ? (CH32RV_PCLK1_HZ + baudrate / 2) / baudrate : 0;
    if (brr < 16 || brr > 0xFFFFul) {
        return;
    }
    _brr = (uint16_t)brr;
#else
    /* The instance is not known yet, so the baud must suit both clocks:
     * PCLK2 (the lower) / 16 at most, PCLK1 / 65535 at least. */
    if (!baudrate || (CH32RV_PCLK2_HZ + baudrate / 2) / baudrate < 16 ||
        (CH32RV_PCLK1_HZ + baudrate / 2) / baudrate > 0xFFFFul) {
        return;
    }
    _baud = baudrate;
#endif
    _config = config;
    uint8_t index = _want_index;
    const ch32rv_route_t *route = _want;
    if (!route) {
        /* No pins named: the fallback, this name's row on its default (or
         * fixed) pins. */
        if (USART_COUNT == 0) {
            return;            /* every USART is Disable on this board */
        }
        index = USART_COUNT == 1 ? 0 : _number;
        route = &usarts[index].def;
        if (route->pins[0] == CH32RV_ROUTE_NO_PIN) {
            return;            /* no default route: pins have to be named */
        }
    }
    bind(index, *route);       /* one call site: bind() is inlined once */
}

uint32_t CH32RVHardwareSerial::baudRate(void) const
{
    if (!_slot) {
        return 0;
    }
    const uint32_t brr = CH32RV_USART_BRR(registers());
    const uint32_t fck = peripheral() == 1 ? CH32RV_PCLK2_HZ : CH32RV_PCLK1_HZ;
    return brr ? (fck + brr / 2u) / brr : 0;
}

void CH32RVHardwareSerial::begin(unsigned long baudrate, uint16_t config, int rxPin,
                               int txPin, bool invert)
{
    if (_slot) {
        end();
    }
    if (invert) {
        return;
    }
    if ((rxPin >= 0 || txPin >= 0) && !setPins(rxPin, txPin)) {
        return;
    }
    begin(baudrate, config);
}

void CH32RVHardwareSerial::end(void)
{
    if (!_slot) {
        return;
    }
    flush();
    close();
}

/* end() without the wait for the last byte: the synchronous mode's end
 * (HardwareSerialSync.cpp), where every transfer waited for its byte and TC
 * is never set if none was made (X035, 2026-11-19: end() waited for ever). */
void CH32RVHardwareSerial::close(void)
{
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->end(*this);     /* DE low, CTS / RTS let go */
    }
    if (CH32RV_SERIAL_DMA_ON(*this, RX)) {
        ch32rv_serial_dma_rx->end(*this);       /* the channel stopped and let go */
    }
    if (CH32RV_SERIAL_DMA_ON(*this, TX)) {
        ch32rv_serial_dma_tx->end(*this);
    }
    const UsartDesc &d = usarts[index_of(_slot)];
    ch32rv_irq_disable(d.irqn);
    CH32RV_USART_CTLR1(d.base) = 0;
    /* Its clock too: a disabled USART still drove its TX low into the pad,
     * and where pads have no alternate-function select (V103, V20x, V30x)
     * that outweighed the other peripheral on it - TIM1 CH2's PWM on PA9
     * stayed low after Serial.end() (V103 EVT, 2026-11-14). */
    ch32rv_clock_disable_at(d.clken_addr, d.clken_mask);
    owner[index_of(_slot)] = nullptr;
    _slot = 0;
    _rx.clear();
    _tx.clear();
    /* The pads go back to inputs, as AVR's and ESP32's end() leave them: an
     * alternate-function TX left behind keeps sending whatever the USART does
     * next, from another begin() on other pads included (V205 2026-11-13:
     * Serial1 ended on PA6 / PA10, begun on PA9 / PA7, sent on PA6 too). */
    release_pin(_tx_pin);
    release_pin(_rx_pin);
}

bool CH32RVHardwareSerial::setPins(int rxPin, int txPin, int ctsPin, int rtsPin)
{
    const bool control = ctsPin >= 0 || rtsPin >= 0;
    if (rxPin < 0 && txPin < 0) {
        /* CTS / RTS alone, the route kept (ESP32: -1 leaves a pin as it is). */
        return control && CH32RVSerialMode::setControlPins(*this, ctsPin, rtsPin);
    }
    if (rxPin < 0 || txPin < 0 || rxPin > 0xFE || txPin > 0xFE) {
        return false;
    }
    /* RX and TX one pin: single-wire half duplex, on a route whose TX it is. */
    const uint8_t tx = (uint8_t)CH32RV_PIN_RESOLVE(txPin);
    const uint8_t rx = rxPin == txPin ? (uint8_t)CH32RV_ROUTE_NO_PIN
                                      : (uint8_t)CH32RV_PIN_RESOLVE(rxPin);
    const Found f = find_route(_number, tx, rx, this);
    if (f.index < 0) {
        return false;
    }
    if (rx == CH32RV_ROUTE_NO_PIN) {
        return CH32RVSerialMode::setOneWire(*this, (uint8_t)f.index, *f.row, ctsPin, rtsPin);
    }
    if (control && !CH32RVSerialMode::setControlPins(*this, ctsPin, rtsPin)) {
        return false;
    }
    useRoute((uint8_t)f.index, f.row);
    return true;
}

void CH32RVHardwareSerial::useRoute(uint8_t index, const ch32rv_route_t *route)
{
    _want = route;
    _want_index = index;
    if (_slot) {
        end();                         /* hands the old pads back */
        bind(_want_index, *_want);
    }
}

uint32_t CH32RVHardwareSerial::registers(void) const
{
    return base();
}

const ch32rv_route_t *CH32RVHardwareSerial::plannedRoute(uint8_t &index) const
{
    if (USART_COUNT == 0) {
        return nullptr;
    }
    index = _slot ? index_of(_slot) : _want ? _want_index : USART_COUNT == 1 ? 0 : _number;
    const ch32rv_route_t *route = _want ? _want : &usarts[index].def;
    return route->pins[0] == CH32RV_ROUTE_NO_PIN ? nullptr : route;
}

bool CH32RVHardwareSerial::setRoute(uint8_t route)
{
    if (USART_COUNT == 0) {
        return false;
    }
    const RouteSet &set = route_sets[USART_COUNT == 1 ? 0 : _number];
    const int r = ch32rv_route_find(set.rows, set.count, route);
    if (r < 0) {
        return false;
    }
    return setPins(set.rows[r].pins[1], set.rows[r].pins[0]);
}

/* ------------------------------------------------------- data */
/* A direction on DMA (setRxBufferSize() / setTxBufferSize()) goes through
 * HardwareSerialDmaRx.cpp / HardwareSerialDmaTx.cpp; every test of it folds
 * away in a sketch without (CH32RV_SERIAL_DMA_ON). */

int CH32RVHardwareSerial::available(void)
{
    if (CH32RV_SERIAL_DMA_ON(*this, RX)) {
        return ch32rv_serial_dma_rx->available(*this);
    }
    return _rx.available();
}

int CH32RVHardwareSerial::availableForWrite(void)
{
    if (CH32RV_SERIAL_DMA_ON(*this, TX)) {
        return ch32rv_serial_dma_tx->room(*this);
    }
    return _slot ? (int)_tx.availableForWrite() : 0;
}

int CH32RVHardwareSerial::peek(void)
{
    if (CH32RV_SERIAL_DMA_ON(*this, RX)) {
        return ch32rv_serial_dma_rx->peek(*this);
    }
    return _rx.peek();
}

int CH32RVHardwareSerial::read(void)
{
    if (CH32RV_SERIAL_DMA_ON(*this, RX)) {
        return ch32rv_serial_dma_rx->read(*this);
    }
    const int c = _rx.pop();
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->read(*this);   /* RTS: room again */
    }
    return c;
}

void CH32RVHardwareSerial::flush(void)
{
    if (!_slot) {
        return;
    }
    const uint32_t base = this->base();
    if (CH32RV_SERIAL_DMA_ON(*this, TX)) {
        ch32rv_serial_dma_tx->flush(*this);
    }
    while (!_tx.isEmpty()) {
    }
    while ((CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TC) == 0u) {
    }
}

size_t CH32RVHardwareSerial::write(uint8_t c)
{
    if (!_slot) {
        return 0;
    }
    const uint32_t base = this->base();
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->write(*this);  /* RS485: the driver on */
    }
    if (CH32RV_SERIAL_DMA_ON(*this, TX)) {
        ch32rv_serial_dma_tx->write(*this, c);
    } else {
        /* Block until the ring has room. The TX interrupt is what drains it, so
         * this cannot deadlock as long as interrupts are enabled. */
        while (_tx.isFull()) {
        }
        _tx.push(c);
        CH32RV_USART_CTLR1(base) |= CH32RV_USART_CTLR1_TXEIE;
    }
    if (ch32rv_serial_mode_ops) {
        ch32rv_serial_mode_ops->written(*this);  /* RS485: watch for the end */
    }
    return 1;
}

/* Before a deep sleep (ch32rv_power.c, by a weak reference: linked only with
 * the low-power code): every open port sends what it holds, or it would stop
 * mid-byte when the clock does. */
extern "C" void ch32rv_serial_sleep_prepare(void)
{
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        if (owner[i]) {
            owner[i]->flush();
        }
    }
}

/* ------------------------------------------------------- serialEvent() */
/* Arduino calls serialEvent() after every loop() when the port has data. main()
 * holds only a weak reference to the dispatcher, so a sketch that never touches
 * Serial links none of this. The dispatcher walks the ports that are open
 * rather than naming Serial1..9, so it pulls in no port the sketch does not
 * use.
 *
 * serialEvent() belongs to Serial and serialEvent1..9() to Serial1..9, by
 * name: the name a port was declared as, not the USART it landed on. */
void serialEvent(void) __attribute__((weak));
void serialEvent1(void) __attribute__((weak));
void serialEvent2(void) __attribute__((weak));
void serialEvent3(void) __attribute__((weak));
void serialEvent4(void) __attribute__((weak));
void serialEvent5(void) __attribute__((weak));
void serialEvent6(void) __attribute__((weak));
void serialEvent7(void) __attribute__((weak));
void serialEvent8(void) __attribute__((weak));
void serialEvent9(void) __attribute__((weak));

uint8_t CH32RVHardwareSerial::takeErrors(void)
{
    noInterrupts();
    const uint8_t errors = _errors;
    _errors = 0;
    interrupts();
    return errors;
}

/* Set by onReceiveError() (HardwareSerialError.cpp): run after loop(). */
void (*ch32rv_serial_error_delivery)(void) = nullptr;

/* Set by the modes beyond plain UART (HardwareSerialMode.cpp). */
const CH32RVSerialModeOps *arduino::ch32rv_serial_mode_ops = nullptr;
/* Set by setRxBufferSize() / setTxBufferSize() (HardwareSerialDmaRx.cpp,
 * HardwareSerialDmaTx.cpp). */
const CH32RVSerialDmaRxOps *arduino::ch32rv_serial_dma_rx = nullptr;
const CH32RVSerialDmaTxOps *arduino::ch32rv_serial_dma_tx = nullptr;

namespace arduino {

void serialEventRun(void)
{
    if (ch32rv_serial_error_delivery) {
        ch32rv_serial_error_delivery();
    }
#if CH32RV_SERIAL_COUNT > 0
    /* One entry for each Serial the board has (a port's number() is below
     * CH32RV_SERIAL_COUNT), not all ten names: on a one-USART chip the table
     * is a single pointer. */
    static void (*const events[CH32RV_SERIAL_COUNT])(void) = {
        serialEvent,
#if CH32RV_SERIAL_COUNT > 1
        serialEvent1,
#endif
#if CH32RV_SERIAL_COUNT > 2
        serialEvent2,
#endif
#if CH32RV_SERIAL_COUNT > 3
        serialEvent3,
#endif
#if CH32RV_SERIAL_COUNT > 4
        serialEvent4,
#endif
#if CH32RV_SERIAL_COUNT > 5
        serialEvent5,
#endif
#if CH32RV_SERIAL_COUNT > 6
        serialEvent6,
#endif
#if CH32RV_SERIAL_COUNT > 7
        serialEvent7,
#endif
#if CH32RV_SERIAL_COUNT > 8
        serialEvent8,
#endif
#if CH32RV_SERIAL_COUNT > 9
        serialEvent9,
#endif
    };
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        CH32RVHardwareSerial *port = owner[i];
        if (!port || port->available() <= 0) {
            continue;
        }
        if (void (*const event)(void) = events[port->number()]) {
            event();
        }
    }
#endif
}

}  // namespace arduino

