/* Does our own crt0 actually hand a correctly set up RAM to setup()?
 *
 * Driven by crt0_probe.py, which fills RAM with a pattern before resetting the
 * part. Everything below is captured in a C++ global constructor - the first
 * user code crt0 runs - and printed later, so what is reported is the state
 * crt0 left behind rather than the state after setup() has had its way with it.
 */
#include <Arduino.h>

extern "C" char _ebss;

/* .data: initialised, so crt0 has to copy it out of flash over the pattern. */
static volatile uint32_t data_marker = 0xA5A5A5A5;
/* .bss: crt0 has to zero it. */
static volatile uint32_t bss_marker;

static uint32_t ctor_marker;
static uint32_t bss_at_ctor;
static uint32_t data_at_ctor;
static uint32_t past_ebss;

struct Early {
  Early() {
    ctor_marker = 0xC0DEC0DE;
    bss_at_ctor = bss_marker;
    data_at_ctor = data_marker;
    /* Nothing initialises the word past _ebss, so the host's pattern should
     * still be sitting there. That is what makes "bss was zero" evidence
     * rather than a coincidence on a part whose RAM happens to power up at 0. */
    past_ebss = *(volatile uint32_t *)&_ebss;
  }
};
static Early early;

static void hex(const char *name, uint32_t v) {
  Serial.print(name);
  Serial.print('=');
  for (int shift = 28; shift >= 0; shift -= 4) {
    Serial.print("0123456789ABCDEF"[(v >> shift) & 0xF]);
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial.println("crt0_probe begin");
  hex("data_at_ctor", data_at_ctor);
  hex("bss_at_ctor", bss_at_ctor);
  hex("ctor", ctor_marker);
  hex("past_ebss", past_ebss);
  hex("data_now", data_marker);
  hex("bss_now", bss_marker);
  Serial.println("crt0_probe done");
}

void loop() {
  delay(1000);
}
