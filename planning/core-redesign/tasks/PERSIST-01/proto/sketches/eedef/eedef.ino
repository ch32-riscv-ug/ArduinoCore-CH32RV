#include <ExpEeprom.h>
uint32_t boots;
void setup() {
  Serial.begin(115200);
  EEPROM.begin(32);
  EEPROM.get(0, boots);
  if (EEPROM.status() != ExpEepromClass::Loaded) boots = 0;
  boots++;
  EEPROM.put(0, boots);
  EEPROM.commit();
}
void loop() { Serial.print("boots "); Serial.println(boots); delay(1000); }
