#include "st.h"
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
volatile uint8_t v;
void setup() { Cfg c; if (!st_load(&c, sizeof c)) memset(&c, 0, sizeof c); c.a += v; st_save(&c, sizeof c); v = c.c; }
void loop() { v++; }
