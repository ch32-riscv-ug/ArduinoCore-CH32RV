/* See Update.h. The work is the core's (ch32rv_update.c). */
#include "Update.h"

#include <CH32RVSystem.h>

UpdateClass Update;

size_t UpdateClass::capacity()
{
    return ch32rv_flash_staging_size();
}

void UpdateClass::led(bool on)
{
    if (_ledPin >= 0) {
        digitalWrite(_ledPin, on ? _ledOn : !_ledOn);
    }
}

bool UpdateClass::begin(size_t size, int command, int ledPin, uint8_t ledOn, const char *label)
{
    (void)label;
    _error = UPDATE_ERROR_OK;
    if (command != U_FLASH) {
        ch32rv_update_abort();
        _error = UPDATE_ERROR_BAD_ARGUMENT;
        return false;
    }
    led(false);
    _ledPin = (int8_t)ledPin;
    _ledOn = ledOn;
    if (_ledPin >= 0) {
        pinMode(_ledPin, OUTPUT);
    }
    const bool ok = ch32rv_update_begin((uint32_t)size, false);
    led(ok);
    return ok;
}

bool UpdateClass::beginRandomAccess(size_t size)
{
    _error = UPDATE_ERROR_OK;
    led(false);
    _ledPin = -1;
    return ch32rv_update_begin((uint32_t)size, true);
}

size_t UpdateClass::write(uint8_t *data, size_t len)
{
    if (!ch32rv_update_write(data, (uint32_t)len)) {
        led(false);
        return 0;
    }
    if (_onProgress) {
        _onProgress(progress(), size());
    }
    return len;
}

size_t UpdateClass::writeStream(Stream &data)
{
    size_t written = 0;
    uint8_t buffer[64];
    while (isRunning() && remaining()) {
        const size_t want = remaining() < sizeof buffer ? remaining() : sizeof buffer;
        const size_t got = data.readBytes(buffer, want);
        if (!got) {
            ch32rv_update_abort();
            _error = UPDATE_ERROR_STREAM;
            led(false);
            break;
        }
        if (write(buffer, got) != got) {
            break;
        }
        written += got;
    }
    return written;
}

bool UpdateClass::writeAt(size_t offset, const uint8_t *data, size_t len)
{
    if (!ch32rv_update_write_at((uint32_t)offset, data, (uint32_t)len)) {
        return false;
    }
    if (_onProgress) {
        _onProgress(progress(), size());
    }
    return true;
}

bool UpdateClass::end(bool evenIfRemaining)
{
    const bool ok = ch32rv_update_end(evenIfRemaining);
    led(false);
    return ok;
}

void UpdateClass::abort()
{
    ch32rv_update_abort();
    led(false);
}

uint8_t UpdateClass::getError()
{
    return _error ? _error : ch32rv_update_error();
}

void UpdateClass::clearError()
{
    _error = UPDATE_ERROR_OK;
    ch32rv_update_clear_error();
}

bool UpdateClass::isRunning()
{
    return ch32rv_update_running();
}

size_t UpdateClass::size()
{
    return ch32rv_update_size();
}

size_t UpdateClass::progress()
{
    return ch32rv_update_progress();
}

bool UpdateClass::isPending()
{
    return ch32rv_update_pending();
}

void UpdateClass::cancelPending()
{
    ch32rv_update_cancel_pending();
}

uint32_t UpdateClass::crc32()
{
    return ch32rv_update_crc32();
}

const char *UpdateClass::errorString()
{
    switch (getError()) {
    case UPDATE_ERROR_OK:           return "No Error";
    case UPDATE_ERROR_WRITE:        return "Flash Write Failed";
    case UPDATE_ERROR_ERASE:        return "Flash Erase Failed";
    case UPDATE_ERROR_READ:         return "Flash Read Failed";
    case UPDATE_ERROR_SPACE:        return "Not Enough Space";
    case UPDATE_ERROR_SIZE:         return "Bad Size Given";
    case UPDATE_ERROR_STREAM:       return "Stream Read Timeout";
    case UPDATE_ERROR_MD5:          return "MD5 Check Failed";
    case UPDATE_ERROR_MAGIC_BYTE:   return "Wrong Magic Byte";
    case UPDATE_ERROR_ACTIVATE:     return "Could Not Activate The Firmware";
    case UPDATE_ERROR_NO_PARTITION: return "Partition Could Not be Found";
    case UPDATE_ERROR_BAD_ARGUMENT: return "Bad Argument";
    case UPDATE_ERROR_ABORT:        return "Aborted";
    case UPDATE_ERROR_VERIFY:       return "Verify Failed";
    }
    return "UNKNOWN";
}

void UpdateClass::printError(Print &out)
{
    out.println(errorString());
}

void UpdateClass::restart()
{
    ch32rv_update_restart();
}

/* CH32RV.restart() installs a pending update too: the CH32RV library's is
 * weak, so this one takes its place in a sketch with Update. */
void arduino::CH32RVSystem::restart()
{
    ch32rv_update_restart();
}
