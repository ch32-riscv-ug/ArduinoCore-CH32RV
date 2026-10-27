#include "st.h"
volatile uint8_t v;
void setup() { uint8_t c = 0; st_load(&c, 1); v = c; }
void loop() { v++; }
