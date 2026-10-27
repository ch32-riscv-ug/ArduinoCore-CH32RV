#include "st.h"
volatile uint8_t v;
void setup() { v = EEPROM.read(0); }
void loop() { v++; }
