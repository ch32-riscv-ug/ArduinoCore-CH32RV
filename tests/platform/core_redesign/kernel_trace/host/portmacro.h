#pragma once
#include <stdint.h>
#include <stddef.h>
#define portSTACK_TYPE uintptr_t
#define portBASE_TYPE intptr_t
#define portUBASE_TYPE uintptr_t
typedef uintptr_t StackType_t;
typedef intptr_t BaseType_t;
typedef uintptr_t UBaseType_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#define portPOINTER_SIZE_TYPE uintptr_t
#define portTICK_TYPE_IS_ATOMIC 1
#define portSTACK_GROWTH (-1)
#define portBYTE_ALIGNMENT 16
#define portTICK_PERIOD_MS (1000u/configTICK_RATE_HZ)
#define portTASK_FUNCTION_PROTO(name,arg) void name(void *arg)
#define portTASK_FUNCTION(name,arg) void name(void *arg)
void experiment_host_yield(void);
void experiment_host_enter(void);
void experiment_host_leave(void);
void experiment_host_enable(void);
UBaseType_t experiment_host_mask(void);
void experiment_host_restore(UBaseType_t saved);
#define portYIELD() experiment_host_yield()
#define portYIELD_FROM_ISR(wake) do { if (wake) experiment_host_yield(); } while (0)
#define portENTER_CRITICAL() experiment_host_enter()
#define portEXIT_CRITICAL() experiment_host_leave()
#define portDISABLE_INTERRUPTS() experiment_host_enter()
#define portENABLE_INTERRUPTS() experiment_host_enable()
#define portSET_INTERRUPT_MASK_FROM_ISR() experiment_host_mask()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(saved) experiment_host_restore(saved)
#define portNOP() ((void)0)
#define portINLINE inline
#define portFORCE_INLINE inline __attribute__((always_inline))
#define portMEMORY_BARRIER() __asm volatile("" ::: "memory")
