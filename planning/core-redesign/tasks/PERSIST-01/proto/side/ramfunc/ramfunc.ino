// CH32RV_RAMFUNC: where does the function run, and does it return the right value? UART line "hc ..." and
// hc_result[] = {magic, address, result} for the debugger.
CH32RV_RAMFUNC uint32_t ramMix(uint32_t a, uint32_t b) { return a * 3u + b; }
volatile uint32_t v = 5;
volatile uint32_t hc_result[3];
void setup() {
  Serial.begin(115200);
  delay(300);
  hc_result[1] = (uint32_t)(uintptr_t)&ramMix;
  hc_result[2] = ramMix(v, 7);
  hc_result[0] = 0x4843u;
}
void loop() {
  Serial.print("hc addr 0x");
  Serial.print(hc_result[1], HEX);
  Serial.print(" r ");
  Serial.println(hc_result[2]);
  delay(500);
}
