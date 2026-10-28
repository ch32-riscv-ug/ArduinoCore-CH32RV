// Count boots in interval_ms (from 500), for boards whose UART is not reachable: read the store with the debugger.
void setup() { Settings s; if (!loadSettings(s)) s = kDefaults; s.interval_ms++; saveSettings(s); }
void loop() {}
