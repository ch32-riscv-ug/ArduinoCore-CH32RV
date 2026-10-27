#include <ExpEeprom.h>
CH32RV_EEPROM_SIZE(14000)
void setup() { EEPROM.begin(14000); EEPROM.commit(); }
void loop() {}
