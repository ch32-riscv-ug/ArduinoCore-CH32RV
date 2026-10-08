#include <stdio.h>
volatile int v = 42;
volatile char sink;
int main(void) { char b[32]; snprintf(b, sizeof b, "v=%d", v); sink = b[0]; for (;;); }
