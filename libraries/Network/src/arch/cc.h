/* lwIP's platform header for the Network library. */
#pragma once
#include <stdint.h>
#include <stdlib.h>

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* No diagnostic output. Assertions are compiled out on the chip (LWIP_NOASSERT
 * in lwipopts.h); the host test build stops on one. */
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#ifdef NETWORK_HOST_TEST
#include <stdio.h>
#define LWIP_PLATFORM_ASSERT(x) do { printf("lwIP assert: %s (%s:%d)\n", x, __FILE__, __LINE__); fflush(stdout); abort(); } while (0)
#else
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)
#endif
