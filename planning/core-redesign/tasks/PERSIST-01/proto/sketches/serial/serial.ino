uint32_t boots;
void setup() { Serial.begin(115200); boots++; }
void loop() { Serial.print("boots "); Serial.println(boots); delay(1000); }
