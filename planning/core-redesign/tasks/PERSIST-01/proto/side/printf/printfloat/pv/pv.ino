// Serial.print of floats and doubles, to compare printFloat in double and in float.
volatile double d[] = {3.14159, 2.5, 0.1, 123.456, -0.004, 1.0 / 3, 100000.5, 1234567.89, 16777217.0, 3.0e9, 5e9, 0.000123456};
volatile float f[] = {3.14159f, 0.1f, 123.456f, 1.0f / 3, 98.6f};
void setup() {
  Serial.begin(115200);
  delay(300);
}
void loop() {
  if (Serial.read() != 'g') return;
  for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) {
    Serial.print("D"); Serial.print(i); Serial.print(' ');
    Serial.print(d[i]); Serial.print(' '); Serial.print(d[i], 6); Serial.print(' '); Serial.println(d[i], 9);
  }
  for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
    Serial.print("F"); Serial.print(i); Serial.print(' ');
    Serial.print(f[i]); Serial.print(' '); Serial.println(f[i], 6);
  }
  Serial.println("END");
}
