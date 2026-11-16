/* CH32RV.getBackupRegister() and the rest (CH32RVSystem.h): the core's
 * ch32rv_backup_*() under the names STM32duino gives them. A file of its own:
 * linked only when used. */
#include "CH32RVSystem.h"

#include "Arduino.h"
#include "internal/ch32rv_rtc.h"

namespace arduino {

uint8_t CH32RVSystem::backupRegisterCount()
{
#ifdef CH32RV_HAS_RTC
    return ch32rv_backup_count();
#else
    return 0;
#endif
}

uint16_t CH32RVSystem::getBackupRegister(uint8_t index)
{
#ifdef CH32RV_HAS_RTC
    return ch32rv_backup_read(index);
#else
    (void)index;
    return 0;
#endif
}

bool CH32RVSystem::setBackupRegister(uint8_t index, uint16_t value)
{
#ifdef CH32RV_HAS_RTC
    return ch32rv_backup_write(index, value) != 0;
#else
    (void)index;
    (void)value;
    return false;
#endif
}

bool CH32RVSystem::backupRegistersKeepPower()
{
#if defined(CH32RV_HAS_RTC) && !defined(CH32RV_RTC_STORE_RAM)
    return true;
#else
    return false;
#endif
}

}  // namespace arduino
