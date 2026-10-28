#include <ch32rv_flash.h>
CH32RV_FLASH_STORE(store, CH32RV_FLASH_ERASE_SIZE);
volatile uint8_t v;
#define OFF ((uint32_t)(uintptr_t)store)
void setup() { uint32_t w[CH32RV_FLASH_WRITE_SIZE / 4]; for (uint32_t &x : w) x = v; bool ok = true;
  ok = ch32rv_flash_erase(OFF, CH32RV_FLASH_ERASE_SIZE);
  v = ok + store[0]; }
void loop() { v++; }
