/* setup1() / loop1() on the V3F (CH32H41x, CPU Core = V5F), as arduino-pico
 * runs them on its second core: a sketch that defines either gets the V3F
 * started before setup(), on a stack of its own (CH32RV_V3F_STACK_SIZE,
 * 4 KB unless the build defines it), taken from the heap then: a static
 * array stayed in every V5F build, the weak test not folding at link time. The V3F's rules are
 * internal/ch32rv_v3f.h's: no Serial, no interrupts, no peripheral the V5F
 * uses. A sketch without them links none of this. */
#include "Arduino.h"
#include "internal/ch32rv_v3f.h"

#include <stdlib.h>

#if defined(CH32RV_RUN_V5F)

#ifndef CH32RV_V3F_STACK_SIZE
#define CH32RV_V3F_STACK_SIZE 4096
#endif

void setup1(void) __attribute__((weak));
void loop1(void) __attribute__((weak));

static void v3f_sketch(void *)
{
    if (setup1) {
        setup1();
    }
    for (;;) {
        if (loop1) {
            loop1();
        }
    }
}

void ch32rv_v3f_sketch_start(void)
{
    uint8_t *stack = (uint8_t *)malloc(CH32RV_V3F_STACK_SIZE);
    if (stack) {
        ch32rv_v3f_launch(v3f_sketch, nullptr, stack + CH32RV_V3F_STACK_SIZE);
    }
}

#endif
