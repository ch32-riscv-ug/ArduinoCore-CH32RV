// Regression test for the heap.
//
// cores/arduino/syscalls.c lives inside core.a, and an archive member is only
// pulled in when something already references it. Nothing in a plain sketch
// references _sbrk, so the linker used to take libgloss's instead - and that
// one is the *semihosting* _sbrk: it issues `ecall`, the CH32 has no handler,
// and the trap lands on the reset vector. Every sketch that touched the heap
// rebooted in a loop and printed nothing at all, not even the lines before the
// first allocation. platform.txt now passes --require-defined=_sbrk.
//
// A global String is deliberate: its constructor runs from .init_array, before
// setup(), so a broken heap takes the sketch down at the earliest point.
//
// printf() has the same root cause and lives in stdio_printf.

String global_string;

// The core's own _sbrk (cores/arduino/ch32_sbrk.c). Calling it with 0 reads the
// current program break without moving it.
extern "C" void *_sbrk(ptrdiff_t incr);

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("heap test start");

  // Reaching this line at all is most of the test: with the semihosting _sbrk
  // the board never got here.
  global_string = "abc";
  global_string += "def";
  Serial.print("string=");
  Serial.println(global_string);
  Serial.print("length=");
  Serial.println(global_string.length());

  // malloc must hand back RAM above .bss, not a null and not a wild pointer.
  extern char _end[];
  extern char _heap_end[];
  char *block = (char *)malloc(64);
  Serial.print("malloc=");
  Serial.println(block >= _end && block + 64 <= _heap_end ? "in range" : "BAD");
  if (block) {
    memset(block, 0x5A, 64);
    Serial.print("readback=");
    Serial.println(block[63] == 0x5A ? "ok" : "BAD");
    free(block);
  }

  // free() must actually give memory back. Pointer identity is not the test -
  // newlib-nano's allocator is free to hand out a different address - so check
  // the property that matters: repeating the same alloc/free must not walk the
  // break forever, which is what a no-op free() looks like.
  void *brk_before = _sbrk(0);
  for (int i = 0; i < 8; i++) {
    void *q = malloc(128);
    free(q);
  }
  Serial.print("free_returns_memory=");
  Serial.println(_sbrk(0) == brk_before ? "ok" : "BAD");

  // Asking for more than the whole heap must fail cleanly, not hang or reset.
  void *huge = malloc(0x7FFFFF);
  Serial.print("oom=");
  Serial.println(huge == NULL ? "null" : "BAD");

  Serial.println("heap test done");
}

void loop()
{
}
