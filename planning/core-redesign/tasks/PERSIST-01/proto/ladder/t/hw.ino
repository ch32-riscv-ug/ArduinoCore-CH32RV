#include "st.h"
// Hardware check of one ST_KIND: many writes against a RAM model, then print one line.
uint8_t model[16];
uint32_t bad, n;
void setup() {
  Serial.begin(115200);
  delay(300);
#if ST_KIND == 3 || ST_KIND == 4
  for (uint32_t j = 0; j < 16; j++) model[j] = EEPROM.read(j);  // whatever was left
  for (n = 0; n < 150; n++) {
    const uint32_t i = (n * 7) % 16; const uint8_t v = (uint8_t)(n * 13 + 5);
    EEPROM.write(i, v); model[i] = v;
    for (uint32_t j = 0; j < 16; j++) if (EEPROM.read(j) != model[j]) bad++;
  }
  struct { uint32_t a; uint16_t b; } c = {0x12345678, 0xBEEF}, d;
  EEPROM.put(3, c); EEPROM.get(3, d);
  if (d.a != c.a || d.b != c.b) bad++;
#else
  uint32_t c, e;
  for (n = 0; n < 20; n++) {
    c = n * 0x01010101u + 7; st_save(&c, 4);
    if (!st_load(&e, 4) || e != c) bad++;
  }
#endif
  const uint32_t t0 = millis(); const uint32_t s0 = CH32RV_SYSTICK_CNT;
  uint32_t w[LF_WORDS]; for (uint32_t i = 0; i < LF_WORDS; i++) w[i] = 0xA5A50000u + i;
  __attribute__((section(".ch32rv_store"), aligned(LF_PAGE))) static uint8_t scratch[LF_PAGE];
#if LF_RESULT
  const bool r = lf_write_page((uint32_t)scratch, w);
#else
  const bool r = true; lf_write_page((uint32_t)scratch, w);
#endif
  const bool ok = !memcmp(scratch, w, LF_PAGE);
  const uint32_t us = (CH32RV_SYSTICK_CNT - s0) / (F_CPU / 1000000u), ms = millis() - t0;
  Serial.print("kind "); Serial.print(ST_KIND); Serial.print(" n "); Serial.print(n); Serial.print(" bad "); Serial.print(bad);
  Serial.print(" raw "); Serial.print(r); Serial.print(ok); Serial.print(" us "); Serial.print(us); Serial.print(" millis "); Serial.println(ms);
}
void loop() {}
