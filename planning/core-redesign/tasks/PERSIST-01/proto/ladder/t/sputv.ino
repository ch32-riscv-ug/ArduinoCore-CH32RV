#include "st.h"
// Sample idiom: the sketch keeps its own version in the struct and falls back to defaults when it differs.
struct Cfg { uint16_t ver; uint16_t b; uint32_t a; uint8_t c, d; };
#define CFG_VER 3
static const Cfg def = {CFG_VER, 50, 1000, 3, 1};
volatile uint8_t v;
void setup() { Cfg c; if (!st_load(&c, sizeof c) || c.ver != CFG_VER) c = def; c.a += v; st_save(&c, sizeof c); v = c.c; }
void loop() { v++; }
