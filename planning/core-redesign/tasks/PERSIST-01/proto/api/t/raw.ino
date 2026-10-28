#include <ch32rv_flash.h>
CH32RV_FLASH_STORE(area, 1);
volatile uint8_t v;
void setup() { uint32_t w[CH32RV_FLASH_PAGE_SIZE / 4]; for (uint32_t &x : w) x = v; v = ch32rv_flash_write_page(area, w) + area[0]; }
void loop() { v++; }
