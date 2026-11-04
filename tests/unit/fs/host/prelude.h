/* Force-included into every C++ file of the host build: api/Common.h
 * declares atexit() for the chip, which glibc's C++ declaration contradicts. */
#pragma once
#include <stdlib.h>
#define atexit arduino_api_atexit
