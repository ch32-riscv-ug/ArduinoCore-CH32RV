#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void fixture_start(void (*driver)(void *));
int fixture_offer(unsigned completion,uint32_t value);
void fixture_drain(void);
void fixture_snapshot(int accepted,uint32_t output[16]);
void vExperimentAssert(void);
#ifdef __cplusplus
}
#endif
