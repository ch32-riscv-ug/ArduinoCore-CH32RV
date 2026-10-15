/* Which USART / I2C / SPI instances a sketch can reach, and which name reaches
 * which.
 *
 * The board menu sets each hardware instance to one of
 *   0 Disable  not in the driver's table: no code, no route, no handler
 *   1 AUTO     bound in begin(); every route the variant knows is accepted
 *   2 FIX      one route, the board's: CH32RV_<KIND>n_FIX_<ROLE> / _V1 / _V2
 * through CH32RV_USARTn_MODE, CH32RV_I2Cn_MODE and CH32RV_SPIn_MODE. A board
 * without the menu leaves every instance on AUTO.
 *
 * Names follow the Arduino convention and count up: Serial, Serial1, ...;
 * Wire, Wire1; SPI, SPI1, SPI2. A name is not tied to an instance number. The
 * enabled (AUTO or FIX) instances, in number order, form the driver's table,
 * and the K-th name (Serial = 0, Serial1 = 1, ...) is row K of it:
 *
 *   - begin() with pins takes any free enabled instance that has those pins as
 *     a route, row K first;
 *   - begin() without pins is the fallback, and takes row K on its default
 *     pins: the FIX pins, or the variant's default route for AUTO. A row whose
 *     variant has no default route (the route moves between packages) fails.
 *
 * Exactly as many names exist as there are enabled instances, so a name the
 * board cannot back does not compile. Each name is defined in a file of its
 * own and is linked only when the sketch uses it.
 *
 * An instance exists in the variant when it defines CH32RV_<KIND>n_CLKEN_ADDR
 * and, with a part number selected, that part does not mark it
 * CH32RV_<KIND>n_ABSENT; its pins (CH32RV_SERIALn_TX ...) are only its default
 * route, which an instance can lack. */
#pragma once

#include "pins_arduino.h"

#define CH32RV_PERIPH_DISABLE 0
#define CH32RV_PERIPH_AUTO    1
#define CH32RV_PERIPH_FIX     2

/* ------------------------------------------------------------------ USART */
#ifndef CH32RV_USART1_MODE
#define CH32RV_USART1_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART2_MODE
#define CH32RV_USART2_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART3_MODE
#define CH32RV_USART3_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART4_MODE
#define CH32RV_USART4_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART5_MODE
#define CH32RV_USART5_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART6_MODE
#define CH32RV_USART6_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART7_MODE
#define CH32RV_USART7_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART8_MODE
#define CH32RV_USART8_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART9_MODE
#define CH32RV_USART9_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_USART10_MODE
#define CH32RV_USART10_MODE CH32RV_PERIPH_AUTO
#endif

#if defined(CH32RV_SERIAL1_CLKEN_ADDR) && !defined(CH32RV_SERIAL1_ABSENT) && CH32RV_USART1_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_1 1
#else
#define CH32RV_USART_ON_1 0
#endif
#if defined(CH32RV_SERIAL2_CLKEN_ADDR) && !defined(CH32RV_SERIAL2_ABSENT) && CH32RV_USART2_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_2 1
#else
#define CH32RV_USART_ON_2 0
#endif
#if defined(CH32RV_SERIAL3_CLKEN_ADDR) && !defined(CH32RV_SERIAL3_ABSENT) && CH32RV_USART3_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_3 1
#else
#define CH32RV_USART_ON_3 0
#endif
#if defined(CH32RV_SERIAL4_CLKEN_ADDR) && !defined(CH32RV_SERIAL4_ABSENT) && CH32RV_USART4_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_4 1
#else
#define CH32RV_USART_ON_4 0
#endif
#if defined(CH32RV_SERIAL5_CLKEN_ADDR) && !defined(CH32RV_SERIAL5_ABSENT) && CH32RV_USART5_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_5 1
#else
#define CH32RV_USART_ON_5 0
#endif
#if defined(CH32RV_SERIAL6_CLKEN_ADDR) && !defined(CH32RV_SERIAL6_ABSENT) && CH32RV_USART6_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_6 1
#else
#define CH32RV_USART_ON_6 0
#endif
#if defined(CH32RV_SERIAL7_CLKEN_ADDR) && !defined(CH32RV_SERIAL7_ABSENT) && CH32RV_USART7_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_7 1
#else
#define CH32RV_USART_ON_7 0
#endif
#if defined(CH32RV_SERIAL8_CLKEN_ADDR) && !defined(CH32RV_SERIAL8_ABSENT) && CH32RV_USART8_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_8 1
#else
#define CH32RV_USART_ON_8 0
#endif
#if defined(CH32RV_SERIAL9_CLKEN_ADDR) && !defined(CH32RV_SERIAL9_ABSENT) && CH32RV_USART9_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_9 1
#else
#define CH32RV_USART_ON_9 0
#endif
#if defined(CH32RV_SERIAL10_CLKEN_ADDR) && !defined(CH32RV_SERIAL10_ABSENT) && CH32RV_USART10_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_USART_ON_10 1
#else
#define CH32RV_USART_ON_10 0
#endif
/* Serial, Serial1 .. Serial<COUNT-1>. USART6..10 exist on V205 (6..8), V30x
 * (UART6..8) and V407 / V467 (6..10). */
#define CH32RV_SERIAL_COUNT (CH32RV_USART_ON_1 + CH32RV_USART_ON_2 + CH32RV_USART_ON_3 + \
                             CH32RV_USART_ON_4 + CH32RV_USART_ON_5 + CH32RV_USART_ON_6 + \
                             CH32RV_USART_ON_7 + CH32RV_USART_ON_8 + CH32RV_USART_ON_9 + \
                             CH32RV_USART_ON_10)

/* -------------------------------------------------------------------- I2C */
#ifndef CH32RV_I2C1_MODE
#define CH32RV_I2C1_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_I2C2_MODE
#define CH32RV_I2C2_MODE CH32RV_PERIPH_AUTO
#endif

#if defined(CH32RV_I2C1_CLKEN_ADDR) && !defined(CH32RV_I2C1_ABSENT) && CH32RV_I2C1_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_I2C_ON_1 1
#else
#define CH32RV_I2C_ON_1 0
#endif
#if defined(CH32RV_I2C2_CLKEN_ADDR) && !defined(CH32RV_I2C2_ABSENT) && CH32RV_I2C2_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_I2C_ON_2 1
#else
#define CH32RV_I2C_ON_2 0
#endif
/* Wire, Wire1. */
#define CH32RV_WIRE_COUNT (CH32RV_I2C_ON_1 + CH32RV_I2C_ON_2)

/* -------------------------------------------------------------------- SPI */
#ifndef CH32RV_SPI1_MODE
#define CH32RV_SPI1_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_SPI2_MODE
#define CH32RV_SPI2_MODE CH32RV_PERIPH_AUTO
#endif
#ifndef CH32RV_SPI3_MODE
#define CH32RV_SPI3_MODE CH32RV_PERIPH_AUTO
#endif

#if defined(CH32RV_SPI1_CLKEN_ADDR) && !defined(CH32RV_SPI1_ABSENT) && CH32RV_SPI1_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_SPI_ON_1 1
#else
#define CH32RV_SPI_ON_1 0
#endif
#if defined(CH32RV_SPI2_CLKEN_ADDR) && !defined(CH32RV_SPI2_ABSENT) && CH32RV_SPI2_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_SPI_ON_2 1
#else
#define CH32RV_SPI_ON_2 0
#endif
#if defined(CH32RV_SPI3_CLKEN_ADDR) && !defined(CH32RV_SPI3_ABSENT) && CH32RV_SPI3_MODE != CH32RV_PERIPH_DISABLE
#define CH32RV_SPI_ON_3 1
#else
#define CH32RV_SPI_ON_3 0
#endif
/* SPI, SPI1, SPI2. */
#define CH32RV_SPI_COUNT (CH32RV_SPI_ON_1 + CH32RV_SPI_ON_2 + CH32RV_SPI_ON_3)
