#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "fixture.h"

enum { Input,Request,RawComplete,Complete,QueueCount };
static StaticQueue_t queue_control[QueueCount];
static uint8_t queue_storage[QueueCount][sizeof(uint32_t)];
static QueueHandle_t queues[QueueCount];
static StaticTask_t application_control[EXP_TASKS],driver_control,idle_control;
static StackType_t application_stack[EXP_TASKS][EXP_STACK_WORDS] __attribute__((aligned(16)));
static StackType_t driver_stack[EXP_STACK_WORDS] __attribute__((aligned(16)));
static StackType_t idle_stack[EXP_STACK_WORDS] __attribute__((aligned(16)));
static TaskHandle_t driver_handle;
static uint32_t displayed,completed,rejected,mismatch,pending_request,pending_result,backend_phase;
static int drain_pending;
#if EXP_HOST
void experiment_host_register_stack(void *,size_t);
void experiment_host_prepare_drain(void);
#endif
void vApplicationGetIdleTaskMemory(StaticTask_t **control,StackType_t **stack,uint32_t *words) {
    *control=&idle_control;*stack=idle_stack;*words=EXP_STACK_WORDS;
}
void vApplicationIdleHook(void) {
    if(drain_pending) {
        drain_pending=0;vTaskResume(driver_handle);taskYIELD();
    }
}
static void send(unsigned queue,uint32_t value) { configASSERT(xQueueSend(queues[queue],&value,portMAX_DELAY)==pdPASS); }
static uint32_t receive(unsigned queue) { uint32_t value=0;configASSERT(xQueueReceive(queues[queue],&value,portMAX_DELAY)==pdPASS);return value; }
static void show(uint32_t value) { displayed=value;++completed; }
static void application(void *unused) {
    (void)unused;
    for(;;) {
        uint32_t value=receive(Input);send(Request,value);
#if EXP_TASKS == 2
        show(receive(Complete));
#endif
    }
}
static void backend(void *unused) {
    (void)unused;
    for(;;) {
        pending_request=receive(Request);backend_phase=1;
        pending_result=receive(RawComplete);backend_phase=2;
        if(pending_request!=pending_result)++mismatch;
        send(Complete,pending_result);backend_phase=0;
    }
}
#if EXP_TASKS == 3
static void renderer(void *unused) { (void)unused;for(;;)show(receive(Complete)); }
#endif
int fixture_offer(unsigned completion,uint32_t value) {
    BaseType_t wake=pdFALSE;
    int accepted=xQueueSendFromISR(queues[completion?RawComplete:Input],&value,&wake)==pdPASS;
    /* The driver has higher priority than every worker, so a batch does not
     * silently run workers between external events. This is not a real IRQ. */
    configASSERT(wake==pdFALSE);
    if(!accepted)++rejected;
    return accepted;
}
void fixture_drain(void) {
    configASSERT(!drain_pending);drain_pending=1;
#if EXP_HOST
    experiment_host_prepare_drain();
#endif
    vTaskSuspend(NULL);
    configASSERT(!drain_pending);
}
void fixture_snapshot(int accepted,uint32_t output[16]) {
    output[0]=(uint32_t)accepted;output[1]=displayed;output[2]=completed;output[3]=rejected;
    output[4]=mismatch;output[5]=backend_phase;output[6]=pending_request;output[7]=pending_result;
    for(unsigned i=0;i<QueueCount;++i) {
        uint32_t value=0;output[8+2*i]=(uint32_t)uxQueueMessagesWaiting(queues[i]);
        if(output[8+2*i])configASSERT(xQueuePeek(queues[i],&value,0)==pdPASS);
        output[9+2*i]=value;
    }
}
void fixture_start(void (*driver)(void *)) {
#if EXP_HOST
    for(unsigned i=0;i<EXP_TASKS;++i)experiment_host_register_stack(application_stack[i],sizeof(application_stack[i]));
    experiment_host_register_stack(driver_stack,sizeof(driver_stack));experiment_host_register_stack(idle_stack,sizeof(idle_stack));
#endif
    for(unsigned i=0;i<QueueCount;++i) { queues[i]=xQueueCreateStatic(1,sizeof(uint32_t),queue_storage[i],&queue_control[i]);configASSERT(queues[i]); }
    configASSERT(xTaskCreateStatic(application,"app",EXP_STACK_WORDS,0,1,application_stack[0],&application_control[0]));
    configASSERT(xTaskCreateStatic(backend,"io",EXP_STACK_WORDS,0,1,application_stack[1],&application_control[1]));
#if EXP_TASKS == 3
    configASSERT(xTaskCreateStatic(renderer,"display",EXP_STACK_WORDS,0,1,application_stack[2],&application_control[2]));
#endif
    driver_handle=xTaskCreateStatic(driver,"driver",EXP_STACK_WORDS,0,2,driver_stack,&driver_control);configASSERT(driver_handle);
    vTaskStartScheduler();vExperimentAssert();
}
