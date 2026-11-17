/* The ADC1 DMA channel's interrupt (internal/ch32rv_adc_run.h), shared by
 * CH32RVAdcStream and analogContinuous(): it calls both hooks. A user that
 * is not linked leaves its weak empty default here, which the link-time
 * optimisation drops - so a sketch with one user gets that user's handler
 * alone, as when the vector was the library's own. analogContinuous()'s
 * hook goes first: CH32RVAdcStream's clears the flags when no stream is
 * active. */
#include "Arduino.h"
#include "internal/ch32rv_adc_run.h"

__attribute__((weak)) void ch32rv_adc_stream_irq(void)
{
}

__attribute__((weak)) void ch32rv_adc_continuous_irq(void)
{
}

void ch32rv_adc_run_link(void)
{
}

CH32RV_INTERRUPT void CH32RV_ADC_DMA_HANDLER(void)
{
    ch32rv_adc_continuous_irq();
    ch32rv_adc_stream_irq();
}
