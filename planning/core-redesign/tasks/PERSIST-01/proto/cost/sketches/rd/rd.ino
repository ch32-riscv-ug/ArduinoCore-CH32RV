#include <EEPROM.h>
volatile uint8_t v;
void setup() { EEPROM_BEGIN v = EEPROM.read(0); }
void loop() { v++; }
