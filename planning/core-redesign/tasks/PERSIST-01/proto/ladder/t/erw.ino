#include "st.h"
volatile uint8_t v;
void setup() { v = EEPROM.read(0); EEPROM.write(0, v + 1); }
void loop() { v++; }
