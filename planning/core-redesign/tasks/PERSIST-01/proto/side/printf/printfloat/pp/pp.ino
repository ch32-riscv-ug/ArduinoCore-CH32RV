volatile float fa = 1.5f;
volatile int v = 3;
void setup() {
  Serial.begin(115200);
  Serial.println(fa * v, 3);
}
void loop() {}
