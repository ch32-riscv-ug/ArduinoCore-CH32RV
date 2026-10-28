#include "fi.h"
CH32RV_FLASH_STORE(store, 2 * CH32RV_FLASH_ERASE_SIZE);
volatile uint8_t v;
void setup() { uint32_t w[CH32RV_FLASH_WRITE_SIZE / 4]; for (uint32_t &x : w) x = v; bool ok = true;
  const uint32_t off = (uint32_t)(uintptr_t)store + (v & 1) * CH32RV_FLASH_ERASE_SIZE;
  ok = fi_erase_page(off) && fi_write_page(off, w);
  v = ok + store[0]; }
void loop() { v++; }
