/* ADC1 runs moved by DMA: the part CH32RVAdcStream and analogContinuous()
 * share (ch32rv_adc_run.c). One run at a time - there is one ADC1 and one
 * DMA channel its requests reach - and analogRead() must wait until it ends
 * (ch32rv_adc_run_restore() gives the ADC back).
 *
 * The DMA channel differs between families and is this file's to know, not
 * its users': DMA1 channel 1 everywhere (ch32-device-data dma.csv), channel
 * 12 on V407 / V467, and channel 1 through the DMAMUX on H41x.
 *
 * A run converts a sequence of up to 16 ADC1 channels at one sample-time step
 * (0 the shortest .. 7 the longest), either back to back (continuous mode,
 * started by software) or one round per trigger (an external event: a timer,
 * a pad's edge - ch32rv_adc_trigger.h). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32RV_ADC_RUN_MAX_CHANNELS 16u

/* DMA flags passed to the handler (the channel's TCIF and HTIF, in place). */
#define CH32RV_ADC_RUN_FULL  (1u << 0)   /* the second half of the buffer is full */
#define CH32RV_ADC_RUN_HALF  (1u << 1)   /* the first half is full */

typedef void (*ch32rv_adc_run_handler_t)(void *context, uint32_t flags);

/* The pins' ADC1 channels into `channels`: false when one is no ADC1 input
 * (X305 / X315: an ADC2..4 pad, whose requests reach no DMA channel here).
 * Each pad is set up as analogRead() does it (analog mode, the ADC started
 * and calibrated). */
bool ch32rv_adc_run_channels(const uint8_t *pins, uint8_t count, uint8_t *channels);
/* The sequence and every channel's sample time at `step`, single conversion,
 * no DMA yet. */
void ch32rv_adc_run_sequence(const uint8_t *channels, uint8_t count, uint8_t step);
/* Rounds per second of the sequence free-running at `step` (measured: the
 * steps' times differ between families and are in no table). 0 if the DMA
 * never finished. Leaves the sequence at `step`. */
uint32_t ch32rv_adc_run_measure(const uint8_t *channels, uint8_t count, uint8_t step);
/* Start a run of the sequence set by ch32rv_adc_run_sequence() into
 * `buffer` (`values` long, circular; the handler called from the DMA's
 * interrupt as each half fills), converting back to back. */
void ch32rv_adc_run_start(uint16_t *buffer, uint32_t values, ch32rv_adc_run_handler_t handler, void *context);
/* The same, one round per trigger of the source `extsel` (the EXTSEL value
 * of the family's regular-group trigger, ch32rv_adc_trigger.h). The caller
 * starts the source after this. */
void ch32rv_adc_run_start_triggered(uint16_t *buffer, uint32_t values, uint8_t extsel,
                                    ch32rv_adc_run_handler_t handler, void *context);
/* Stop: the ADC powered down and up with nothing pending, the DMA off. */
void ch32rv_adc_run_stop(void);
/* Stop a triggered run: the trigger selection back to software, then as
 * ch32rv_adc_run_stop(). */
void ch32rv_adc_run_stop_triggered(void);
/* The values the DMA still has to move before it wraps (its counter). */
uint32_t ch32rv_adc_run_remaining(void);
/* The DMA flags (CH32RV_ADC_RUN_HALF / _FULL) raised and not yet handled. */
uint32_t ch32rv_adc_run_pending(void);
/* Give the ADC back to analogRead(): its single-conversion, software-started
 * set-up and longest sample times. */
void ch32rv_adc_run_restore(void);

#ifdef __cplusplus
}
#endif
