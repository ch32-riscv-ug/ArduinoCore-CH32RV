#include <EEPROM.h>
volatile uint8_t v;
void setup() { EEPROM_BEGIN v = EEPROM.read(0); EEPROM.write(0, v + 1); EEPROM_COMMIT }
void loop() { v++; }
