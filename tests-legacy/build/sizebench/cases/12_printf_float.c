#include <stdio.h>
volatile float f = 1.5f;
int main(void) { printf("f=%f\n", (double)f); for (;;); }
