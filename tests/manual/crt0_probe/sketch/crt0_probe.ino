/* Does our own crt0 actually hand a correctly set up RAM to setup()?
 *
 * Driven by crt0_probe.py, which fills RAM with a pattern before resetting the
 * part. Everything below is captured in a C++ global constructor - the first
 * user code crt0 runs - and reported on request, so what is reported is the
 * state crt0 left behind rather than the state after setup() has had its way
 * with it.
 *
 * Speaks the command protocol (tests/TEST_PLAN.ja.md) like every other sketch
 * on this bench. It does not need the protocol for the usual reason - the
 * driver resets the part itself, so it is already listening when the first
 * line appears - but for the other one: the WCH-Link's UART bridge only pushes
 * a packet out when it has one, so a sketch that says its piece and then goes
 * quiet can leave its last line stuck inside the bridge. The repeated banner
 * keeps the pipe moving, and RUN can be asked again without reflashing.
 */
#include <Arduino.h>

#include "testcmd.h"

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
     * rather than a coincidence on a part whose RAM happens to power up at 0.
     * The value is not checked here: the host owns the pattern, so the host
     * compares it. Duplicating the constant would only let the two drift. */
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

static void run_checks() {
  /* The raw words first: they are the evidence, and a failing check is far
   * easier to read next to the value that produced it. */
  hex("data_at_ctor", data_at_ctor);
  hex("bss_at_ctor", bss_at_ctor);
  hex("ctor", ctor_marker);
  hex("past_ebss", past_ebss);
  hex("data_now", data_marker);
  hex("bss_now", bss_marker);

  tc_check("bss_zeroed", bss_at_ctor == 0);
  tc_check("data_copied_from_flash", data_at_ctor == 0xA5A5A5A5);
  tc_check("init_array_ran", ctor_marker == 0xC0DEC0DE);
  tc_done();
}

void setup() {
  tc_begin("crt0_probe");
}

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) {
    return;
  }
  if (!strcmp(cmd, "RUN")) {
    run_checks();
  } else {
    tc_unknown(cmd);
  }
}
