// Read back right after writing, in the same run: 8 writes of different patterns to the same page, plus the range
// check (a page outside the store, and an unaligned one, must be refused).
#include <ch32rv_flash.h>
CH32RV_FLASH_STORE(store, 1);
void setup() {
  Serial.begin(115200);
  delay(300);
  uint32_t w[CH32RV_FLASH_PAGE_SIZE / 4];
  int bad = 0, fail = 0;
  for (uint32_t k = 0; k < 8; k++) {
    for (uint32_t i = 0; i < CH32RV_FLASH_PAGE_SIZE / 4; i++) w[i] = 0x01010101u * (k * 37 + i);
    fail += !ch32rv_flash_write_page(store, w);
    for (uint32_t i = 0; i < CH32RV_FLASH_PAGE_SIZE / 4; i++) bad += ((const volatile uint32_t *)store)[i] != w[i];
  }
  const bool outside = ch32rv_flash_write_page((const void *)0x100, w), unaligned = ch32rv_flash_write_page(store + 4, w);
  Serial.print("readback interval bad ");
  Serial.print(bad);
  Serial.print(" fail ");
  Serial.print(fail);
  Serial.print(" outside ");
  Serial.print(outside);
  Serial.print(" unaligned ");
  Serial.println(unaligned);
}
void loop() {}
