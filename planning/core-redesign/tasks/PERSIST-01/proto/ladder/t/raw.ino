#include "st.h"
__attribute__((section(".ch32rv_store"), aligned(LF_PAGE))) uint8_t area[LF_PAGE];
volatile uint8_t v;
void setup() { uint32_t w[LF_WORDS]; memset(w, 0xFF, sizeof w); w[0] = v; lf_write_page((uint32_t)area, w); v = area[0]; }
void loop() { v++; }
