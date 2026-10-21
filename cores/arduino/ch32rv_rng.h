/* The hardware random number generator, on the series that have one
 * (CH32V303/V305/V307/V317, V407/V467, H415-H417: CH32RV_RNG_BASE). It is the
 * entropy for TLS keys and TCP sequence numbers; nothing else in the core
 * starts it. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fills out with len bytes from the generator, starting it on first use.
 * false when the series has no generator, or it reported a seed or clock
 * error that did not clear: the caller must not use what is in out then. */
bool ch32rv_rng_read(void *out, size_t len);

#ifdef __cplusplus
}
#endif
