#include "st.h"
__attribute__((section(".ch32rv_store"), aligned(64))) uint8_t area[64];
volatile uint8_t v;
void setup() { uint32_t w[16]; memset(w, 0xFF, sizeof w); w[0] = v; lf_write_page((uint32_t)area, w); v = area[0]; }
void loop() { v++; }
