/* The bus clocks, for code that is compiled before Arduino.h is (the
 * HardwareSerial header lays out its class by them, so every translation unit
 * must see the same values).
 *
 * HCLK: what the peripherals and timers count; F_CPU except on CH32H41x with
 * the V5F (internal/ch32rv_clock.h). APB1 runs at HCLK. APB2 does too, except
 * where a CPU Clock entry would take the ADC (PCLK2 / 8 at most) past its
 * ceiling: V20x / V30x at 120 and 144 MHz halve it (CH32RV_CLOCK_PPRE2_DIV,
 * boards.txt), which moves USART1, SPI1 and the ADC. The APB2 timers still
 * count HCLK: an APB prescaler other than /1 doubles their clock back. */
#pragma once

#ifndef CH32RV_HCLK_HZ
#define CH32RV_HCLK_HZ F_CPU
#endif
#ifndef CH32RV_CLOCK_PPRE2_DIV
#define CH32RV_CLOCK_PPRE2_DIV 1
#endif
#define CH32RV_PCLK1_HZ CH32RV_HCLK_HZ
#define CH32RV_PCLK2_HZ (CH32RV_HCLK_HZ / CH32RV_CLOCK_PPRE2_DIV)
