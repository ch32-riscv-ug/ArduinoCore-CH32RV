/* The bus clocks, for code that is compiled before Arduino.h is (the
 * HardwareSerial header lays out its class by them, so every translation unit
 * must see the same values).
 *
 * HCLK: what the peripherals and timers count; F_CPU except on CH32H41x with
 * the V5F (internal/ch32rv_clock.h). Each APB bus runs at HCLK unless a CPU
 * Clock entry divides it (boards.txt): APB2 where the ADC (PCLK2 / 8 at most)
 * would pass its ceiling - USART1, SPI1, the ADC move; APB1 where I2C would
 * pass its 60 MHz (FREQ's range on every family, the reference manuals) -
 * USART2 and up, SPI2 / 3 and I2C move. A timer counts its bus's clock, twice
 * that when the bus is divided (CH32RV_TIMCLK1_HZ / 2_HZ): halving a bus
 * leaves its timers on HCLK, quartering it does not. */
#pragma once

#ifndef CH32RV_HCLK_HZ
#define CH32RV_HCLK_HZ F_CPU
#endif
#ifndef CH32RV_CLOCK_PPRE2_DIV
#define CH32RV_CLOCK_PPRE2_DIV 1
#endif
#ifndef CH32RV_CLOCK_PPRE1_DIV
#define CH32RV_CLOCK_PPRE1_DIV 1
#endif
#define CH32RV_PCLK1_HZ (CH32RV_HCLK_HZ / CH32RV_CLOCK_PPRE1_DIV)
#define CH32RV_PCLK2_HZ (CH32RV_HCLK_HZ / CH32RV_CLOCK_PPRE2_DIV)
#define CH32RV_TIMCLK1_HZ (CH32RV_CLOCK_PPRE1_DIV == 1 ? CH32RV_PCLK1_HZ : 2u * CH32RV_PCLK1_HZ)
#define CH32RV_TIMCLK2_HZ (CH32RV_CLOCK_PPRE2_DIV == 1 ? CH32RV_PCLK2_HZ : 2u * CH32RV_PCLK2_HZ)
