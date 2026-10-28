// Does the CPU stop while the flash is busy? rewrite_page() three times with interrupts off, timed by the SysTick
// counter (the core's tick ISR zeroes it, so it only counts across a span with interrupts off), then three times with
// interrupts on, timed by millis(). A millis() advance near the measured time means the tick ISR kept running.
// Result: UART line "stall ..." and st_result[] (magic, us x3, ms x3, micros() x3) for the debugger. Six erases of one unit.
#include <ch32rv_flash.h>
#include <ch32rv_irq.h>
#include <ch32rv_registers.h>

CH32RV_FLASH_STORE(store, CH32RV_FLASH_ERASE_SIZE);
volatile uint32_t st_result[10];
static uint32_t page[CH32RV_FLASH_WRITE_SIZE / 4];

void setup() {
  Serial.begin(115200);
  delay(300);
  for (uint32_t i = 0; i < 3; i++) {
    page[0] = i;
    const uint32_t irq = ch32rv_irq_save();
    const uint32_t c0 = CH32RV_SYSTICK_CNT;
    ch32rv_flash_rewrite_page(CH32RV_FLASH_OFFSET(store), page);
    const uint32_t c1 = CH32RV_SYSTICK_CNT;
    ch32rv_irq_restore(irq);
    st_result[1 + i] = (c1 - c0) / (F_CPU / 1000000u);
  }
  for (uint32_t i = 0; i < 3; i++) {
    page[0] = 10 + i;
    delay(2);
    const uint32_t m0 = millis(), u0 = micros();
    ch32rv_flash_rewrite_page(CH32RV_FLASH_OFFSET(store), page);
    const uint32_t u1 = micros();
    st_result[4 + i] = millis() - m0;
    st_result[7 + i] = u1 - u0;
  }
  st_result[0] = 0x57A11u;
  Serial.print("stall us");
  for (int i = 1; i < 4; i++) { Serial.print(' '); Serial.print(st_result[i]); }
  Serial.print(" ms");
  for (int i = 4; i < 7; i++) { Serial.print(' '); Serial.print(st_result[i]); }
  Serial.print(" micros");
  for (int i = 7; i < 10; i++) { Serial.print(' '); Serial.print(st_result[i]); }
  Serial.println();
}

void loop() {}
