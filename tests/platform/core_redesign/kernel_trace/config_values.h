#pragma once
#include <stdint.h>
#define configMTIME_BASE_ADDRESS 0
#define configMTIMECMP_BASE_ADDRESS 0
#define configCPU_CLOCK_HZ 48000000u
#define configTICK_RATE_HZ 500u
#define configUSE_PREEMPTION EXP_PREEMPT
#define configUSE_TIME_SLICING EXP_PREEMPT
#define configMAX_PRIORITIES 3
#define configMINIMAL_STACK_SIZE EXP_STACK_WORDS
#define configMAX_TASK_NAME_LEN 8
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION 0
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 0
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 0
#define configUSE_MUTEXES 0
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TIMERS 0
#define configUSE_CO_ROUTINES 0
#define configUSE_TRACE_FACILITY 0
#define configQUEUE_REGISTRY_SIZE 0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_APPLICATION_TASK_TAG 0
#define configUSE_TASK_NOTIFICATIONS 0
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 0
#define INCLUDE_vTaskDelayUntil 0
#define INCLUDE_xTaskGetSchedulerState 0
#define INCLUDE_uxTaskPriorityGet 0
#define INCLUDE_vTaskPrioritySet 0
#define INCLUDE_xTaskGetCurrentTaskHandle 0
void vExperimentAssert(void);
#define configASSERT(value) do { if (!(value)) vExperimentAssert(); } while (0)
