volatile uint8_t v;
void setup() { Serial.begin(115200); Serial.println(v); }
void loop() { v++; }
