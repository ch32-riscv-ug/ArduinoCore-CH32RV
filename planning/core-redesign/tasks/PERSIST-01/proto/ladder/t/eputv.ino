#include "st.h"
// Sample idiom (the usual AVR one): a version byte at offset 0; defaults when it differs.
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
#define CFG_VER 3
static const Cfg def = {1000, 50, 3, 1};
volatile uint8_t v;
void setup() {
  Cfg c;
  if (EEPROM.read(0) != CFG_VER) { c = def; EEPROM.put(1, c); EEPROM.write(0, CFG_VER); } else EEPROM.get(1, c);
  c.a += v; EEPROM.put(1, c); v = c.c;
}
void loop() { v++; }
