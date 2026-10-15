#include "HardwareSerial.h"

#include "Arduino.h"
#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"

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
            if (set.rows[r].pins[0] == tx && set.rows[r].pins[1] == rx) {
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
__attribute__((always_inline)) inline void CH32RVHardwareSerial::irq(uint32_t base)
{
    const uint16_t status = CH32RV_USART_STATR(base);

    /* RXNEIE also raises the interrupt for the receive error flags, and those
     * are only cleared by reading STATR and then DATAR. Handling just RXNE
     * leaves an overrun asserted, the interrupt re-enters immediately and the
     * core never returns to loop() - which is what a noisy or unconnected RX
     * line produces. So always drain the data register when any receive flag
     * is set, and only keep the byte when it is a real one: a frame that came
     * with a framing, noise or parity error is a glitch on the line (a probe
     * re-opening its UART put one such byte in front of the next command line,
     * 2026-09-22), not data. An overrun still leaves this byte valid. */
    if (status & (CH32RV_USART_STATR_RXNE | CH32RV_USART_STATR_ORE |
                  CH32RV_USART_STATR_NE | CH32RV_USART_STATR_FE |
                  CH32RV_USART_STATR_PE)) {
        const uint8_t data = (uint8_t)CH32RV_USART_DATAR(base);
        if ((status & CH32RV_USART_STATR_RXNE) &&
            !(status & (CH32RV_USART_STATR_FE | CH32RV_USART_STATR_NE |
                        CH32RV_USART_STATR_PE))) {
            _rx.push(data);
        }
    }
    if (status & CH32RV_USART_STATR_TXE) {
        if (_tx.isEmpty()) {
            CH32RV_USART_CTLR1(base) &= (uint16_t)~CH32RV_USART_CTLR1_TXEIE;
        } else {
            CH32RV_USART_DATAR(base) = (uint16_t)(uint8_t)_tx.pop();
        }
    }
}

extern "C" __attribute__((interrupt)) void ch32rv_usart_irq(void)
{
    if (USART_COUNT == 1) {          /* one vector, so no need to ask which */
        if (owner[0]) {
            owner[0]->irq(usarts[0].base);
        }
        return;
    }
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));
    const uint8_t irqn = (uint8_t)(cause & 0xFFFu);
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        if (usarts[i].irqn == irqn) {
            if (owner[i]) {
                owner[i]->irq(usarts[i].base);
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

    /* USART1 hangs off PCLK2 and the others off PCLK1; the core leaves both
     * APB prescalers at /1, so either way the clock is HCLK, which SystemInit
     * makes equal to F_CPU. BRR holds USARTDIV * 16, which is exactly the
     * rounded fck/baud. */
    CH32RV_USART_BRR(d.base) = _brr;

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
    switch (_config & SERIAL_STOP_BIT_MASK) {
    case SERIAL_STOP_BIT_2:   ctlr2 |= CH32RV_USART_CTLR2_STOP_2; break;
    case SERIAL_STOP_BIT_1_5: ctlr2 |= CH32RV_USART_CTLR2_STOP_1P5; break;
    default: break;
    }
    CH32RV_USART_CTLR2(d.base) = ctlr2;
    CH32RV_USART_CTLR3(d.base) = 0;
    CH32RV_USART_CTLR1(d.base) = ctlr1 | CH32RV_USART_CTLR1_UE;
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
    /* BRR holds USARTDIV * 16 in 16 bits, and below 16 (USARTDIV < 1) the
     * USART does not refuse it: it runs at fck / 16 whatever was asked
     * (measured on X035 and V003, 2026-09-29: 921600 at 8 MHz came out at
     * 500 kbaud). Above 0xFFFF the value would be cut to 16 bits. Either way
     * the port would talk at a rate nobody asked for, so an unreachable baud
     * leaves it closed. The range is fck / 65535 .. fck / 16 (48 MHz: 733 baud
     * .. 3 Mbaud). */
    const unsigned long brr = baudrate ? (F_CPU + baudrate / 2) / baudrate : 0;
    if (brr < 16 || brr > 0xFFFFul) {
        return;
    }
    _brr = (uint16_t)brr;
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
    const UsartDesc &d = usarts[index_of(_slot)];
    ch32rv_irq_disable(d.irqn);
    CH32RV_USART_CTLR1(d.base) = 0;
    owner[index_of(_slot)] = nullptr;
    _slot = 0;
    _rx.clear();
    _tx.clear();
}

bool CH32RVHardwareSerial::setPins(int rxPin, int txPin, int ctsPin, int rtsPin)
{
    if (ctsPin >= 0 || rtsPin >= 0) {
        return false;                  /* no hardware flow control */
    }
    if (rxPin < 0 || txPin < 0 || rxPin > 0xFE || txPin > 0xFE) {
        return false;
    }
    const Found f = find_route(_number, (uint8_t)CH32RV_PIN_RESOLVE(txPin),
                               (uint8_t)CH32RV_PIN_RESOLVE(rxPin), this);
    if (f.index < 0) {
        return false;
    }
    _want = f.row;
    _want_index = (uint8_t)f.index;
    if (_slot) {
        const uint8_t old_tx = _tx_pin, old_rx = _rx_pin;
        end();
        release_pin(old_tx);
        release_pin(old_rx);
        bind(_want_index, *_want);
    }
    return true;
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
int CH32RVHardwareSerial::available(void)
{
    return _rx.available();
}

int CH32RVHardwareSerial::availableForWrite(void)
{
    return _slot ? (int)_tx.availableForWrite() : 0;
}

int CH32RVHardwareSerial::peek(void)
{
    return _rx.peek();
}

int CH32RVHardwareSerial::read(void)
{
    return _rx.pop();
}

void CH32RVHardwareSerial::flush(void)
{
    if (!_slot) {
        return;
    }
    const uint32_t base = usarts[index_of(_slot)].base;
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
    const uint32_t base = usarts[index_of(_slot)].base;
#if defined(CH32RV_VARIANT_CH32X035) || defined(CH32RV_VARIANT_CH32X033)
    /* X035's (and the same die's X033) USART TXE interrupt path is not yet reliable on every USART:
     * a burst larger than the 63-byte ring capacity can otherwise wait here
     * forever. Polling TXE is bounded by one character time and keeps Serial
     * correct while the interrupt-route issue is investigated. */
    while ((CH32RV_USART_STATR(base) & CH32RV_USART_STATR_TXE) == 0u) {
    }
    CH32RV_USART_DATAR(base) = c;
    return 1;
#else
    /* Block until the ring has room. The TX interrupt is what drains it, so
     * this cannot deadlock as long as interrupts are enabled. */
    while (_tx.isFull()) {
    }
    _tx.push(c);
    CH32RV_USART_CTLR1(base) |= CH32RV_USART_CTLR1_TXEIE;
    return 1;
#endif
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

namespace arduino {

void serialEventRun(void)
{
    static void (*const events[])(void) = {serialEvent, serialEvent1, serialEvent2,
                                           serialEvent3, serialEvent4, serialEvent5,
                                           serialEvent6, serialEvent7, serialEvent8,
                                           serialEvent9};
    for (uint8_t i = 0; i < USART_COUNT; i++) {
        CH32RVHardwareSerial *port = owner[i];
        if (!port || port->available() <= 0) {
            continue;
        }
        const uint8_t n = port->number();
        if (n < sizeof events / sizeof events[0] && events[n]) {
            events[n]();
        }
    }
}

}  // namespace arduino

