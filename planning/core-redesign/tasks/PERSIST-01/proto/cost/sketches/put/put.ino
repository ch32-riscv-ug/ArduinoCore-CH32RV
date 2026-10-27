#include <EEPROM.h>
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
volatile uint8_t v;
void setup() { EEPROM_BEGIN Cfg c; EEPROM.get(0, c); c.a += v; EEPROM.put(0, c); EEPROM_COMMIT v = c.c; }
void loop() { v++; }
