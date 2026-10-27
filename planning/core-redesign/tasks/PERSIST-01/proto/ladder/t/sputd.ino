#include "st.h"
// Like sput, but a failed load falls back to non-zero defaults (what a real sketch does).
struct Cfg { uint32_t a; uint16_t b; uint8_t c, d; };
static const Cfg def = {1000, 50, 3, 1};
volatile uint8_t v;
void setup() { Cfg c; if (!st_load(&c, sizeof c)) c = def; c.a += v; st_save(&c, sizeof c); v = c.c; }
void loop() { v++; }
