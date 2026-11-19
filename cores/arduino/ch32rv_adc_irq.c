/* ADC1's interrupt (internal/ch32rv_adc_run.h): CH32RVAdcInjected's end of an injected round. Linked by
 * ch32rv_adc_irq_link() only: a sketch without CH32RVAdcInjected leaves the vector alone. */
#include "Arduino.h"
#include "internal/ch32rv_adc_run.h"
#include "internal/ch32rv_interrupt.h"

#if defined(CH32RV_ADC_IRQ_HANDLER)
__attribute__((weak)) void ch32rv_adc_injected_irq(void)
{
}

void ch32rv_adc_irq_link(void)
{
}

CH32RV_INTERRUPT void CH32RV_ADC_IRQ_HANDLER(void)
{
    ch32rv_adc_injected_irq();
}
#endif
