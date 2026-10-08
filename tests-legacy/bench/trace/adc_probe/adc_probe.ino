// analogRead on request, for tests-legacy/bench/trace/adc_probe. Pins are the core's pin numbers (((port + 2) << 5) | bit),
// derived by the host from the bench file - no jig's table is baked in.
//   ADC <pin> [n]          -> "ADC <pin> n=<n> min=<> max=<> mean=<>" (n samples, default 16)
//   ADCSEQ <pin> [n]       -> first / last 8 of n back-to-back conversions (settling / decay evidence)
//   CH <channel> <n> <pin> -> raw regular-channel conversion (15 = VREFINT) after analogRead(<pin>) set the ADC up
// requires: ram=4K
// (the sample buffers and the line buffer do not fit CH32V003's 2 KB next to the stack)
#define TC_CMD_MAX 64
#include "testcmd.h"
#include "ch32rv_registers.h"   // raw ADC register access for the CH command

void setup() { tc_begin("adc_probe"); }

void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}; unsigned pin = 0, n = 16, setup_pin = 0;
  const int got = sscanf(cmd, "%7s %u %u %u", verb, &pin, &n, &setup_pin);
  if (got < 2 || (strcmp(verb, "ADC") && strcmp(verb, "ADCSEQ") && strcmp(verb, "CH"))) { Console.print("ERR "); Console.println(cmd); return; }
  if (!strcmp(verb, "CH")) {   // raw channel: reuse the core's ADC setup, then point RSQR3 at the channel
    const unsigned ch = pin; if (ch > 15 || got < 4) { Console.println("ERR ch"); return; }
    (void)analogRead((pin_size_t)setup_pin);
    if (n > 1000) n = 1000;
    long sum = 0; int lo = 4096, hi = -1, first = -1, last = -1;
    for (unsigned i = 0; i < n; ++i) {
      CH32RV_ADC_RSQR1 = 0; CH32RV_ADC_RSQR3 = ch; CH32RV_ADC_CTLR2 |= CH32RV_ADC_CTLR2_SWSTART;
      while ((CH32RV_ADC_STATR & CH32RV_ADC_STATR_EOC) == 0u) {}
      const int v = (int)(CH32RV_ADC_RDATAR & 0xFFFu) >> (CH32RV_ADC_BITS - 10);   // 10-bit like analogRead() (V003 is 10-bit natively)
      sum += v; if (v < lo) lo = v; if (v > hi) hi = v; if (i == 0) first = v; last = v;
    }
    Console.print("CH "); Console.print(ch); Console.print(" n="); Console.print(n); Console.print(" min="); Console.print(lo);
    Console.print(" max="); Console.print(hi); Console.print(" mean="); Console.print((long)(sum / (long)n));
    Console.print(" first="); Console.print(first); Console.print(" last="); Console.println(last);
    return;
  }
  if (!digitalPinIsValid((pin_size_t)pin)) { Console.print("ERR pin "); Console.println(pin); return; }
  pinMode((pin_size_t)pin, INPUT);
  if (!strcmp(verb, "ADCSEQ")) {   // print the first 8 and the last 8 of n back-to-back conversions
#if defined(__riscv_e)
    static int seq[200]; if (n > 200) n = 200;      // 2 KiB of RAM on the V00x
#else
    static int seq[1000]; if (n > 1000) n = 1000;
#endif
    for (unsigned i = 0; i < n; ++i) seq[i] = analogRead((pin_size_t)pin);
    Console.print("ADCSEQ "); Console.print(pin); Console.print(" first=");
    for (unsigned i = 0; i < 8 && i < n; ++i) { Console.print(seq[i]); Console.print(i + 1 < 8 ? "," : ""); }
    Console.print(" last=");
    for (unsigned i = (n > 8 ? n - 8 : 0); i < n; ++i) { Console.print(seq[i]); Console.print(i + 1 < n ? "," : ""); }
    Console.println();
    return;
  }
  long sum = 0; int lo = 4096, hi = -1;
  for (unsigned i = 0; i < n; ++i) { const int v = analogRead((pin_size_t)pin); sum += v; if (v < lo) lo = v; if (v > hi) hi = v; }
  Console.print("ADC "); Console.print(pin); Console.print(" n="); Console.print(n); Console.print(" min="); Console.print(lo);
  Console.print(" max="); Console.print(hi); Console.print(" mean="); Console.println((long)(sum / (long)n));
}
