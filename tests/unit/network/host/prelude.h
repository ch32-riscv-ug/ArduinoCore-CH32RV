/* Force-included (-include) into every C++ file of the host test build.
 * api/Common.h declares atexit() for the chip, and glibc's C++ declaration
 * (noexcept) contradicts it: with stdlib.h in first, the API's one is renamed
 * out of the way. */
#pragma once
#include <stdlib.h>
#define atexit arduino_api_atexit
