// A function placed in .highcode: where does it run, and does it return the right value?
__attribute__((section(".highcode"), noinline)) uint32_t ramMix(uint32_t a, uint32_t b) { return a * 3u + b; }
volatile uint32_t v = 5;
void setup() {
  Serial.begin(115200);
  delay(300);
}
void loop() {
  const uint32_t r = ramMix(v, 7);
  Serial.print("hc addr 0x");
  Serial.print((uint32_t)(uintptr_t)&ramMix, HEX);
  Serial.print(" r ");
  Serial.println(r);
  delay(500);
}
