volatile uint8_t v;
void setup() { Settings s; if (!loadSettings(s)) s = kDefaults; s.level += v; v = saveSettings(s) + s.mode; }
void loop() { v++; }
