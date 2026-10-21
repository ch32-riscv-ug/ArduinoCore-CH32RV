#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stdint.h>

/* Compile-only fixture: the external producer supplies ordered virtual input
 * and completion tokens. No PD, I2C or physical IRQ source is inferred. */
enum { Input,Request,RawComplete,Complete,QueueCount };
#if EXP_USED
static StaticQueue_t queue_control[QueueCount];
static uint8_t queue_storage[QueueCount][sizeof(uint32_t)];
static QueueHandle_t queues[QueueCount];
static StaticTask_t application_control[EXP_TASKS],idle_control;
static StackType_t application_stack[EXP_TASKS][EXP_STACK_WORDS] __attribute__((aligned(16)));
static StackType_t idle_stack[EXP_STACK_WORDS] __attribute__((aligned(16)));
volatile uint32_t fixture_kind,fixture_value,displayed,completed,rejected,mismatch;

void vApplicationGetIdleTaskMemory(StaticTask_t **control,StackType_t **stack,uint32_t *words) {
    *control=&idle_control;*stack=idle_stack;*words=EXP_STACK_WORDS;
}
static void send(unsigned queue,uint32_t value) {
    configASSERT(xQueueSend(queues[queue],&value,portMAX_DELAY)==pdPASS);
}
static uint32_t receive(unsigned queue) {
    uint32_t value=0;
    configASSERT(xQueueReceive(queues[queue],&value,portMAX_DELAY)==pdPASS);
    return value;
}
static void show(uint32_t value) { displayed=value;++completed; }
static void application(void *unused) {
    (void)unused;
    for (;;) {
        const uint32_t value=receive(Input);
        send(Request,value);
#if EXP_TASKS == 2
        show(receive(Complete));
#endif
    }
}
static void backend(void *unused) {
    (void)unused;
    for (;;) {
        const uint32_t request=receive(Request),done=receive(RawComplete);
        if (request!=done) ++mismatch;
        send(Complete,done);
    }
}
#if EXP_TASKS == 3
static void renderer(void *unused) { (void)unused;for (;;) show(receive(Complete)); }
#endif
/* This symbol is retained for structural inspection. It has no vector and is
 * not invoked by the probe. A real fixture must supply its interrupt context. */
void fixture_irq(void) {
    BaseType_t wake=pdFALSE;
    const unsigned queue=fixture_kind ? RawComplete : Input;
    const uint32_t value=fixture_value;
    if (xQueueSendFromISR(queues[queue],&value,&wake)!=pdPASS) ++rejected;
    portYIELD_FROM_ISR(wake);
}
void vExperimentAssert(void) { for (;;) __asm volatile(""); }
extern void SysTick_Handler(void),SW_Handler(void);
void (* const fixture_entries[])(void) __attribute__((used,section(".fixture_roots")))={fixture_irq,SysTick_Handler,SW_Handler};
void probe_main(void) __attribute__((section(".text.entry"),noreturn));
void probe_main(void) {
    for (unsigned i=0;i<QueueCount;++i) {
        queues[i]=xQueueCreateStatic(1,sizeof(uint32_t),queue_storage[i],&queue_control[i]);
        configASSERT(queues[i]);
    }
    configASSERT(xTaskCreateStatic(application,"app",EXP_STACK_WORDS,0,1,application_stack[0],&application_control[0]));
    configASSERT(xTaskCreateStatic(backend,"io",EXP_STACK_WORDS,0,1,application_stack[1],&application_control[1]));
#if EXP_TASKS == 3
    configASSERT(xTaskCreateStatic(renderer,"display",EXP_STACK_WORDS,0,1,application_stack[2],&application_control[2]));
#endif
    vTaskStartScheduler();
    vExperimentAssert();
    for (;;) __asm volatile("");
}
#else
void probe_main(void) __attribute__((section(".text.entry"),noreturn));
void probe_main(void) { for (;;) __asm volatile(""); }
#endif
