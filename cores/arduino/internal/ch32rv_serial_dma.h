/* What HardwareSerialDmaRx.cpp and HardwareSerialDmaTx.cpp share: each
 * enabled USART's DMA line for one direction, in HardwareSerial.cpp's table
 * order (its CH32RV_USART_HAS_n are exactly these CH32RV_USART_ON_n), and the
 * sizes setRxBufferSize() / setTxBufferSize() take. For those two files only. */
#pragma once

#include "HardwareSerial.h"
#include "internal/ch32rv_dma.h"

/* A USART the variant names no channel for: channel 0, the built-in path. */
#define CH32RV_DMA_NO_LINE CH32RV_DMA_LINE(0, 0, 0u, 0u, 0u, 0u)

#ifdef CH32RV_SERIAL1_DMA_RX
#define CH32RV_SDMA_1_rx CH32RV_SERIAL1_DMA_RX
#else
#define CH32RV_SDMA_1_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL1_DMA_TX
#define CH32RV_SDMA_1_tx CH32RV_SERIAL1_DMA_TX
#else
#define CH32RV_SDMA_1_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_1
#define CH32RV_SDMA_ROW_1(dir) CH32RV_SDMA_1_##dir,
#else
#define CH32RV_SDMA_ROW_1(dir)
#endif
#ifdef CH32RV_SERIAL2_DMA_RX
#define CH32RV_SDMA_2_rx CH32RV_SERIAL2_DMA_RX
#else
#define CH32RV_SDMA_2_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL2_DMA_TX
#define CH32RV_SDMA_2_tx CH32RV_SERIAL2_DMA_TX
#else
#define CH32RV_SDMA_2_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_2
#define CH32RV_SDMA_ROW_2(dir) CH32RV_SDMA_2_##dir,
#else
#define CH32RV_SDMA_ROW_2(dir)
#endif
#ifdef CH32RV_SERIAL3_DMA_RX
#define CH32RV_SDMA_3_rx CH32RV_SERIAL3_DMA_RX
#else
#define CH32RV_SDMA_3_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL3_DMA_TX
#define CH32RV_SDMA_3_tx CH32RV_SERIAL3_DMA_TX
#else
#define CH32RV_SDMA_3_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_3
#define CH32RV_SDMA_ROW_3(dir) CH32RV_SDMA_3_##dir,
#else
#define CH32RV_SDMA_ROW_3(dir)
#endif
#ifdef CH32RV_SERIAL4_DMA_RX
#define CH32RV_SDMA_4_rx CH32RV_SERIAL4_DMA_RX
#else
#define CH32RV_SDMA_4_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL4_DMA_TX
#define CH32RV_SDMA_4_tx CH32RV_SERIAL4_DMA_TX
#else
#define CH32RV_SDMA_4_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_4
#define CH32RV_SDMA_ROW_4(dir) CH32RV_SDMA_4_##dir,
#else
#define CH32RV_SDMA_ROW_4(dir)
#endif
#ifdef CH32RV_SERIAL5_DMA_RX
#define CH32RV_SDMA_5_rx CH32RV_SERIAL5_DMA_RX
#else
#define CH32RV_SDMA_5_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL5_DMA_TX
#define CH32RV_SDMA_5_tx CH32RV_SERIAL5_DMA_TX
#else
#define CH32RV_SDMA_5_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_5
#define CH32RV_SDMA_ROW_5(dir) CH32RV_SDMA_5_##dir,
#else
#define CH32RV_SDMA_ROW_5(dir)
#endif
#ifdef CH32RV_SERIAL6_DMA_RX
#define CH32RV_SDMA_6_rx CH32RV_SERIAL6_DMA_RX
#else
#define CH32RV_SDMA_6_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL6_DMA_TX
#define CH32RV_SDMA_6_tx CH32RV_SERIAL6_DMA_TX
#else
#define CH32RV_SDMA_6_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_6
#define CH32RV_SDMA_ROW_6(dir) CH32RV_SDMA_6_##dir,
#else
#define CH32RV_SDMA_ROW_6(dir)
#endif
#ifdef CH32RV_SERIAL7_DMA_RX
#define CH32RV_SDMA_7_rx CH32RV_SERIAL7_DMA_RX
#else
#define CH32RV_SDMA_7_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL7_DMA_TX
#define CH32RV_SDMA_7_tx CH32RV_SERIAL7_DMA_TX
#else
#define CH32RV_SDMA_7_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_7
#define CH32RV_SDMA_ROW_7(dir) CH32RV_SDMA_7_##dir,
#else
#define CH32RV_SDMA_ROW_7(dir)
#endif
#ifdef CH32RV_SERIAL8_DMA_RX
#define CH32RV_SDMA_8_rx CH32RV_SERIAL8_DMA_RX
#else
#define CH32RV_SDMA_8_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL8_DMA_TX
#define CH32RV_SDMA_8_tx CH32RV_SERIAL8_DMA_TX
#else
#define CH32RV_SDMA_8_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_8
#define CH32RV_SDMA_ROW_8(dir) CH32RV_SDMA_8_##dir,
#else
#define CH32RV_SDMA_ROW_8(dir)
#endif
#ifdef CH32RV_SERIAL9_DMA_RX
#define CH32RV_SDMA_9_rx CH32RV_SERIAL9_DMA_RX
#else
#define CH32RV_SDMA_9_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL9_DMA_TX
#define CH32RV_SDMA_9_tx CH32RV_SERIAL9_DMA_TX
#else
#define CH32RV_SDMA_9_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_9
#define CH32RV_SDMA_ROW_9(dir) CH32RV_SDMA_9_##dir,
#else
#define CH32RV_SDMA_ROW_9(dir)
#endif
#ifdef CH32RV_SERIAL10_DMA_RX
#define CH32RV_SDMA_10_rx CH32RV_SERIAL10_DMA_RX
#else
#define CH32RV_SDMA_10_rx CH32RV_DMA_NO_LINE
#endif
#ifdef CH32RV_SERIAL10_DMA_TX
#define CH32RV_SDMA_10_tx CH32RV_SERIAL10_DMA_TX
#else
#define CH32RV_SDMA_10_tx CH32RV_DMA_NO_LINE
#endif
#if CH32RV_USART_ON_10
#define CH32RV_SDMA_ROW_10(dir) CH32RV_SDMA_10_##dir,
#else
#define CH32RV_SDMA_ROW_10(dir)
#endif
/* The table for `dir` (rx, tx: not RX / TX, which pins_arduino.h defines as pads), one more row than USARTs (never empty). */
#define CH32RV_SERIAL_DMA_LINES(dir) { \
    CH32RV_SDMA_ROW_1(dir) CH32RV_SDMA_ROW_2(dir) CH32RV_SDMA_ROW_3(dir) CH32RV_SDMA_ROW_4(dir) CH32RV_SDMA_ROW_5(dir) \
    CH32RV_SDMA_ROW_6(dir) CH32RV_SDMA_ROW_7(dir) CH32RV_SDMA_ROW_8(dir) CH32RV_SDMA_ROW_9(dir) CH32RV_SDMA_ROW_10(dir) \
    CH32RV_DMA_NO_LINE }

/* The size setRxBufferSize() / setTxBufferSize() use, or 0 for one they
 * refuse: an open port (as on an ESP32), and outside 2 .. 65534 (CNTR's 16
 * bits). Even, so that the half-transfer flag marks half the buffer. */
static inline size_t ch32rv_serial_dma_size(bool open, size_t size)
{
    return open || size < 2u || size > 65534u ? 0u : (size & ~(size_t)1u);
}

/* i, n - 1 at most after one step past the end. */
static inline uint32_t ch32rv_serial_dma_wrap(uint32_t i, uint32_t n)
{
    return i >= n ? i - n : i;
}
