/* The heap across CH32H41x's two cores: with the sketch on the V5F (CPU Core = V5F) the V3F may run the sketch's
 * code as well (setup1() / loop1(), CH32RV.startV3F()), and picolibc's malloc has no lock of its own (it is built
 * without one: no __retarget_lock_*). The link wraps its entry points (-Wl,--wrap=..., the CPU Core menu's
 * ld_layout_flags, V5F items only) and each runs inside this lock: this core's interrupts masked and HSEM channel
 * CH32RV_HSEM_HEAP_CHANNEL taken, as ESP-IDF's heap does with a spinlock and its interrupts off. Measured without
 * it (2026-11-19, h417qeu6-pico, both cores allocating 8-256 B for 5 s): the V5F found 34 blocks corrupted and the
 * V3F stopped; with it none in 610,850 + 118,560 rounds, the V5F's rounds some 11 % fewer (the waits included).
 *
 * Nested on one core - realloc's own malloc and free, reached through the wraps too - only counts: the outer one
 * holds the channel. The masked interrupts make malloc safe from this core's handlers as well.
 *
 * Until the V3F is given a function, ch32rv_cores_shared is 0 (internal/ch32rv_cores.h): a sketch that never
 * starts it has no store to the flag, LTO folds it, and each wrap is a jump to the real function. */
#include <malloc.h>
#include <stddef.h>

#include "internal/ch32rv_registers.h"   /* CH32RV_IRQN_HSEM and internal/ch32rv_cores.h */

#if defined(CH32RV_IRQN_HSEM) && defined(CH32RV_RUN_V5F)

static uint8_t depth[2];      /* per core: the lock's nesting */
static uint32_t saved[2];     /* per core: the interrupts' state the outermost lock found */

static inline __attribute__((always_inline)) void heap_lock(void)
{
    if (!ch32rv_cores_shared) {
        return;
    }
    const uint32_t irq = ch32rv_irq_save();
    const uint32_t core = ch32rv_core_id();
    if (depth[core]++ == 0u) {
        while (!ch32rv_hsem_read_lock(CH32RV_HSEM_HEAP_CHANNEL)) {
        }
        __asm__ volatile ("fence iorw, iorw" ::: "memory");
        saved[core] = irq;
    }
}

static inline __attribute__((always_inline)) void heap_unlock(void)
{
    if (!ch32rv_cores_shared) {
        return;
    }
    const uint32_t core = ch32rv_core_id();
    if (--depth[core] == 0u) {
        ch32rv_hsem_unlock(CH32RV_HSEM_HEAP_CHANNEL);
        ch32rv_irq_restore(saved[core]);
    }
}

void *__real_malloc(size_t size);
void __real_free(void *p);
void *__real_realloc(void *p, size_t size);
void *__real_memalign(size_t align, size_t size);
void *__real_aligned_alloc(size_t align, size_t size);
struct mallinfo __real_mallinfo(void);

void *__wrap_malloc(size_t size)
{
    heap_lock();
    void *p = __real_malloc(size);
    heap_unlock();
    return p;
}

void __wrap_free(void *p)
{
    heap_lock();
    __real_free(p);
    heap_unlock();
}

void *__wrap_realloc(void *p, size_t size)
{
    heap_lock();
    void *q = __real_realloc(p, size);
    heap_unlock();
    return q;
}

/* calloc, posix_memalign, valloc, pvalloc and reallocarray go through these. */
void *__wrap_memalign(size_t align, size_t size)
{
    heap_lock();
    void *p = __real_memalign(align, size);
    heap_unlock();
    return p;
}

void *__wrap_aligned_alloc(size_t align, size_t size)
{
    heap_lock();
    void *p = __real_aligned_alloc(align, size);
    heap_unlock();
    return p;
}

struct mallinfo __wrap_mallinfo(void)
{
    heap_lock();
    const struct mallinfo m = __real_mallinfo();
    heap_unlock();
    return m;
}

#endif
