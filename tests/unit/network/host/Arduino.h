/* The Network library's host test build: the core's ArduinoCore-API copy, the
 * few core functions the library calls, and nothing of the chip. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "api/ArduinoAPI.h"
#include "api/Client.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void ch32rv_usb_task(void) __attribute__((weak));
extern void ch32rv_usb_host_task(void) __attribute__((weak));
extern void ch32rv_net_task(void) __attribute__((weak));
#ifdef __cplusplus
}
#endif
using namespace arduino;
