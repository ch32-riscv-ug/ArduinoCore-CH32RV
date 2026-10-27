#include <ExpEeprom.h>
CH32RV_EEPROM_SIZE(1024)
extern const uint8_t blob[11800];
const uint8_t blob[11800] = {1};
void setup() { Serial.begin(115200); EEPROM.begin(1024); Serial.println(blob[millis() % sizeof blob]); EEPROM.commit(); }
void loop() {}
