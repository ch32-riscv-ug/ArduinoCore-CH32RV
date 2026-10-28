// M19 functional test of every flash function on one series. Result: UART line "ft ..." and ft_result[] in RAM
// (magic, failed-step bitmap, steps run, erased word seen) for boards whose UART the bench does not reach.
#include <ch32rv_flash.h>

CH32RV_FLASH_STORE(store, 2 * CH32RV_FLASH_ERASE_SIZE);
#define OFF ((uint32_t)(uintptr_t)store)
#define EW (CH32RV_FLASH_ERASE_SIZE / 4)
#define WW (CH32RV_FLASH_WRITE_SIZE / 4)

volatile uint32_t ft_result[4];
static uint32_t buf[WW + 1];
static uint32_t fails, steps;

static const volatile uint32_t *fw(uint32_t i) { return (const volatile uint32_t *)store + i; }
static bool erased(uint32_t from, uint32_t n) {
  for (uint32_t i = from; i < from + n; i++) if (*fw(i) != CH32RV_FLASH_ERASED_WORD) return false;
  return true;
}
static void fill(uint32_t seed) { for (uint32_t i = 0; i < WW + 1; i++) buf[i] = seed * 0x9E3779B1u + i * 0x01010101u; }
static bool same(uint32_t at, const uint8_t *src) {
  for (uint32_t i = 0; i < WW; i++) { uint32_t w; memcpy(&w, src + 4 * i, 4); if (*fw(at + i) != w) return false; }
  return true;
}
static void step(bool ok) { if (!ok) fails |= 1u << steps; steps++; }

void setup() {
  Serial.begin(115200);
  delay(300);
  ft_result[2] = *fw(0);
  // 0-1: erase_page, then the whole unit reads as the erased word
  step(ch32rv_flash_erase_page(OFF));
  step(erased(0, EW));
  // 2-3: write_page into the erased unit
  fill(1); step(ch32rv_flash_write_page(OFF, buf)); step(same(0, (const uint8_t *)buf));
  // 4-5: rewrite_page over it
  fill(2); step(ch32rv_flash_rewrite_page(OFF, buf)); step(same(0, (const uint8_t *)buf) && erased(WW, EW - WW));
  // 6-7: erase two units in one call
  step(ch32rv_flash_erase(OFF, 2 * CH32RV_FLASH_ERASE_SIZE)); step(erased(0, 2 * EW));
  // 8-9: write from unaligned data (one byte in), into the second unit
  fill(3); step(ch32rv_flash_write(OFF + CH32RV_FLASH_ERASE_SIZE, (const uint8_t *)buf + 1, CH32RV_FLASH_WRITE_SIZE));
  step(same(EW, (const uint8_t *)buf + 1));
  // 10-11: erase the second unit again, then its last write unit through write_page; the rest stays erased
  fill(4); step(ch32rv_flash_erase_page(OFF + CH32RV_FLASH_ERASE_SIZE) && ch32rv_flash_write_page(OFF + 2 * CH32RV_FLASH_ERASE_SIZE - CH32RV_FLASH_WRITE_SIZE, buf));
  step(same(2 * EW - WW, (const uint8_t *)buf) && erased(EW, EW - WW));
  // 12-15: refused: the program itself (XIP series), unaligned, past the end of flash, zero length
#if !CH32RV_FLASH_SELF_WRITE
  step(!ch32rv_flash_erase_page(0));
#else
  step(true);
#endif
  step(!ch32rv_flash_erase_page(OFF + 4));
  step(!ch32rv_flash_erase(OFF, 3 * CH32RV_FLASH_ERASE_SIZE + 0x100000u));
  step(!ch32rv_flash_write(OFF, buf, 0));
  ft_result[0] = 0xF7F7F7F7u;
  ft_result[1] = fails;
  ft_result[3] = steps;
  Serial.print("ft steps ");
  Serial.print(steps);
  Serial.print(" fails 0x");
  Serial.print(fails, HEX);
  Serial.print(" first 0x");
  Serial.println(ft_result[2], HEX);
}

void loop() {}
