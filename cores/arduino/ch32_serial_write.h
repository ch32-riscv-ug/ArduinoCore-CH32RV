/* Bridge from the C syscall layer to the C++ serial instance.
 * Kept in its own header so syscalls.c does not have to be C++. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes to the board's monitor port. Returns the number of bytes accepted;
 * 0 when no port has been begun, so printf() before Serial.begin() is a
 * silent no-op rather than a hang. */
size_t ch32_serial_write_bytes(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
