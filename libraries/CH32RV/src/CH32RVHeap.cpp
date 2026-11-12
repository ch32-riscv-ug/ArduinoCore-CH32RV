/* getFreeHeap() on its own: mallinfo() links newlib's mstats.o, which brings
 * malloc_stats() and with it stdio (fiprintf, __sf: 312 B of RAM on a V003).
 * In CH32RVSystem.cpp it came with any CH32RV call - resetReason(), restart() -
 * since the library is linked from an archive one object at a time. */
#include "CH32RVSystem.h"

#include "Arduino.h"

#include <malloc.h>
#include <stddef.h>
#include <stdint.h>

extern "C" {
void *sbrk(ptrdiff_t incr);
extern char _heap_end[];
}

uint32_t CH32RVSystem::getFreeHeap()
{
    const char *brk = (const char *)sbrk(0);
    const struct mallinfo mi = mallinfo();
    return (uint32_t)(_heap_end - brk) + (uint32_t)mi.fordblks;
}
