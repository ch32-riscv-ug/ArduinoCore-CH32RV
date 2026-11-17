/* CH32RV.attachExtiLine() and the PVD (CH32RVSystem.h), on the core's
 * ch32rv_exti_line.c and ch32rv_pvd.c. A file of its own: linked only when
 * used. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_exti_line.h"
#include "internal/ch32rv_pvd.h"

namespace arduino {

bool CH32RVSystem::attachExtiLine(uint8_t line, void (*callback)(), PinStatus mode)
{
#ifdef CH32RV_EXTI_LINE_MASK
    return ch32rv_exti_line_attach(line, callback, mode) != 0;
#else
    (void)line;
    (void)callback;
    (void)mode;
    return false;
#endif
}

void CH32RVSystem::detachExtiLine(uint8_t line)
{
#ifdef CH32RV_EXTI_LINE_MASK
    ch32rv_exti_line_detach(line);
#else
    (void)line;
#endif
}

void CH32RVSystem::triggerExtiLine(uint8_t line)
{
#ifdef CH32RV_EXTI_LINE_MASK
    ch32rv_exti_line_trigger(line);
#else
    (void)line;
#endif
}

uint32_t CH32RVSystem::setLowVoltageDetect(uint32_t millivolts, void (*callback)())
{
#if CH32RV_HAS_PVD
    return ch32rv_pvd_set(millivolts, callback);
#else
    (void)millivolts;
    (void)callback;
    return 0;
#endif
}

void CH32RVSystem::clearLowVoltageDetect()
{
#if CH32RV_HAS_PVD
    ch32rv_pvd_clear();
#endif
}

bool CH32RVSystem::isLowVoltage()
{
#if CH32RV_HAS_PVD
    return ch32rv_pvd_low() != 0;
#else
    return false;
#endif
}

uint32_t CH32RVSystem::lowVoltageLevel(uint8_t index)
{
#if CH32RV_HAS_PVD
    return ch32rv_pvd_level_mv(index);
#else
    (void)index;
    return 0;
#endif
}

}  // namespace arduino
