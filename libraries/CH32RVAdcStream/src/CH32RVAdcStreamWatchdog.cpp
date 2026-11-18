/* The analog watchdog of CH32RVAdcStream (setWatchdog()): linked only with
 * it. CTLR1's AWDEN turns it on for the regular channels, AWDSGL limits it to
 * the one in AWDCH; WDHTR / WDLTR hold the window; STATR's AWD latches a value
 * outside it (every family here, ch32-device-data registers.csv). */
#include "CH32RVAdcStream.h"

#include "internal/ch32rv_registers.h"

namespace {
constexpr uint32_t AWDCH_MASK = 0x1Fu;
constexpr uint32_t AWDSGL = 1u << 9;
constexpr uint32_t AWDEN = 1u << 23;
constexpr uint32_t STATR_AWD = 1u << 0;
#define ADC_WDHTR CH32RV_REG32(CH32RV_ADC1_BASE + 0x24u)
#define ADC_WDLTR CH32RV_REG32(CH32RV_ADC1_BASE + 0x28u)
}  // namespace

void CH32RVAdcStream::watchdogApply()
{
    uint32_t ctlr1 = CH32RV_ADC_CTLR1 & ~(AWDCH_MASK | AWDSGL | AWDEN);
    if (_wdIndex != -2) {
        ADC_WDHTR = _wdHigh;
        ADC_WDLTR = _wdLow;
        ctlr1 |= AWDEN;
        if (_wdIndex >= 0) {
            ctlr1 |= AWDSGL | _channels[_wdIndex];
        }
    }
    CH32RV_ADC_CTLR1 = ctlr1;
}

bool CH32RVAdcStream::setWatchdog(uint16_t low, uint16_t high, int8_t pinIndex)
{
    if (!_begun || low > high || pinIndex < -1 || pinIndex >= (int8_t)_count) {
        return false;
    }
    _wdLow = low;
    _wdHigh = high;
    _wdIndex = pinIndex;
    watchdogApply();
    CH32RV_ADC_STATR = ~STATR_AWD;          /* rc_w0: only AWD cleared */
    return true;
}

bool CH32RVAdcStream::watchdogTripped()
{
    if (!(CH32RV_ADC_STATR & STATR_AWD)) {
        return false;
    }
    CH32RV_ADC_STATR = ~STATR_AWD;          /* rc_w0: only AWD cleared */
    return true;
}

void CH32RVAdcStream::clearWatchdog()
{
    _wdIndex = -2;
    if (_begun) {
        watchdogApply();
    }
    CH32RV_ADC_STATR = ~STATR_AWD;          /* rc_w0: only AWD cleared */
}
