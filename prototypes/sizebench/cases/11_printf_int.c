#include <stdio.h>
volatile int v = 42;
int main(void) { printf("v=%d\n", v); for (;;); }
