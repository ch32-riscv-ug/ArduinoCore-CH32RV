// printf lines of several lengths over Serial: what arrives? Each line is followed by a marker written without printf,
// and by whether a malloc of that size works.
#include <stdlib.h>
static const int lens[] = {60, 62, 63, 64, 65, 100, 200, 400, 800};
void setup() {
  Serial.begin(115200);
  delay(300);
}
void loop() {
  if (Serial.read() != 'g') return;
  for (int L : lens) {
    const size_t n = Serial.printf("L%03d %0*d\n", L, L - 5, 0);  // L characters, then the newline
    void *p = malloc(L + 2);
    Serial.print("M ");
    Serial.print(L);
    Serial.print(" ret ");
    Serial.print((unsigned)n);
    Serial.print(" malloc ");
    Serial.println(p ? 1 : 0);
    free(p);
  }
  Serial.println("END");
}
