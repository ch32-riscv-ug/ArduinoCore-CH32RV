#pragma once
#include <stdint.h>
/* Fixed soft-float RV32I call-boundary context, no TLS or per-context gp/tp. */
typedef struct __attribute__((aligned(16))) {
    uint32_t ra,sp,saved[12],padding[2];
} ExperimentContext;
#ifdef __cplusplus
extern "C" {
#endif
void experiment_context_swap(ExperimentContext *old_context,const ExperimentContext *next_context);
#ifdef __cplusplus
}
static_assert(sizeof(ExperimentContext)==64,"fixed assembly layout");
#endif
