// printf of long lines when the heap is short: FILL bytes of static RAM are taken first (build with -DFILL=...).
#include <stdlib.h>
#ifndef FILL
#define FILL 0
#endif
volatile uint8_t fill[FILL + 1];
static const int lens[] = {63, 64, 100, 200, 400};
void setup() {
  Serial.begin(115200);
  delay(300);
  fill[0] = 1;
}
void loop() {
  if (Serial.read() != 'g') return;
  size_t big = 0;  // the largest malloc that works, by 16 B steps
  for (size_t n = 16; n < 2048; n += 16) {
    void *p = malloc(n);
    if (!p) break;
    free(p);
    big = n;
  }
  Serial.print("M heap ");
  Serial.println((unsigned)big);
  for (int L : lens) {
    const size_t n = Serial.printf("L%03d %0*d\n", L, L - 5, 0);
    Serial.print("M ");
    Serial.print(L);
    Serial.print(" ret ");
    Serial.println((unsigned)n);
  }
  Serial.println("END");
}
