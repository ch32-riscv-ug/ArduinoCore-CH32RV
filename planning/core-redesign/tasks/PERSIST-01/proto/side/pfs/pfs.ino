// How deep does Serial.printf go into the stack? Paint below the stack pointer, print, find the lowest word touched.
extern "C" char _eusrstack[], _heap_end[];
static const int lens[] = {10, 63, 64, 200};
static uint32_t depthOf(int L, int mode) {  // 0 nothing, 1 printf, 2 print
  uint32_t sp;
  asm volatile("mv %0, sp" : "=r"(sp));
  uint32_t *lo = (uint32_t *)((uintptr_t)_heap_end - 256), *hi = (uint32_t *)(sp - 64);
  for (uint32_t *p = lo; p < hi; p++) *p = 0xA5A5A5A5u;
  if (mode == 1) Serial.printf("L%03d %0*d\n", L, L - 5, 0);
  if (mode == 2) { Serial.print("P"); Serial.println(L); }
  Serial.flush();
  uint32_t *p = lo;
  while (p < hi && *p == 0xA5A5A5A5u) p++;
  return (uint32_t)((uintptr_t)_eusrstack - (uintptr_t)p);
}
void setup() {
  Serial.begin(115200);
  delay(300);
}
void loop() {
  if (Serial.read() != 'g') return;
  for (int L : lens) {
    const uint32_t d = depthOf(L, 1);
    Serial.print("M ");
    Serial.print(L);
    Serial.print(" stack ");
    Serial.println(d);
  }
  const uint32_t none = depthOf(0, 0), print = depthOf(5, 2);
  Serial.print("M none stack ");
  Serial.println(none);
  Serial.print("M print stack ");
  Serial.println(print);
  Serial.println("END");
}
