// analogRead on request: ADC <pin_name> [n]  -> "ADC <name> n=<n> min=<> max=<> mean=<>" (n samples, default 16)
// ADCSEQ <pin_name> [n]: first/last 8 of n back-to-back conversions (settling / decay evidence)
// CH <channel> [n]: raw regular-channel conversion after analogRead(PA0) set the ADC up (15 = VREFINT)
#define TC_CMD_MAX 64
#include "testcmd.h"
#include "ch32_registers.h"   // raw ADC register access for the CH command
struct Named { const char *name; uint8_t pin; };
#if defined(OEP_TARGET_V003)   // UIAPduino jig: the wired ADC pads (PD3/PD4 are USB, PD5/PD6 the console)
static const Named kPins[] = {{"PA1", PA1}, {"PA2", PA2}, {"PC4", PC4}, {"PD2", PD2}};
#define ADC_SETUP_PIN PA1
#else
static const Named kPins[] = {{"PA0", PA0}, {"PA1", PA1}, {"PA2", PA2}, {"PA3", PA3}, {"PA4", PA4}, {"PA5", PA5}, {"PA6", PA6}, {"PA7", PA7}};
#define ADC_SETUP_PIN PA0
#endif
void setup() { tc_begin("adc_probe"); }
void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}, name[6] = {0}; unsigned n = 16;
  if (sscanf(cmd, "%7s %5s %u", verb, name, &n) < 2 || (strcmp(verb, "ADC") && strcmp(verb, "ADCSEQ") && strcmp(verb, "CH"))) { Console.print("ERR "); Console.println(cmd); return; }
  if (!strcmp(verb, "CH")) {   // raw channel: reuse the core's ADC setup, then point RSQR3 at the channel
    const unsigned ch = (unsigned)atoi(name); if (ch > 15) { Console.println("ERR ch"); return; }
    (void)analogRead(ADC_SETUP_PIN);
    if (n > 1000) n = 1000;
    long sum = 0; int lo = 4096, hi = -1, first = -1, last = -1;
    for (unsigned i = 0; i < n; ++i) {
      CH32_ADC_RSQR1 = 0; CH32_ADC_RSQR3 = ch; CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_SWSTART;
      while ((CH32_ADC_STATR & CH32_ADC_STATR_EOC) == 0u) {}
      const int v = (int)(CH32_ADC_RDATAR & 0xFFFu) >> (CH32_ADC_BITS - 10);   // 10-bit like analogRead() (V003 is 10-bit natively)
      sum += v; if (v < lo) lo = v; if (v > hi) hi = v; if (i == 0) first = v; last = v;
    }
    Console.print("CH "); Console.print(ch); Console.print(" n="); Console.print(n); Console.print(" min="); Console.print(lo);
    Console.print(" max="); Console.print(hi); Console.print(" mean="); Console.print((long)(sum / (long)n));
    Console.print(" first="); Console.print(first); Console.print(" last="); Console.println(last);
    return;
  }
  int pin = -1; for (const Named &p : kPins) if (!strcmp(p.name, name)) pin = p.pin;
  if (pin < 0) { Console.print("ERR pin "); Console.println(name); return; }
  pinMode((pin_size_t)pin, INPUT);
  if (!strcmp(verb, "ADCSEQ")) {   // print the first 8 and the last 8 of n back-to-back conversions
#if defined(OEP_TARGET_V003)
    static int seq[200]; if (n > 200) n = 200;      // 2 KiB of RAM on the V003
#else
    static int seq[1000]; if (n > 1000) n = 1000;
#endif
    for (unsigned i = 0; i < n; ++i) seq[i] = analogRead((pin_size_t)pin);
    Console.print("ADCSEQ "); Console.print(name); Console.print(" first=");
    for (unsigned i = 0; i < 8 && i < n; ++i) { Console.print(seq[i]); Console.print(i + 1 < 8 ? "," : ""); }
    Console.print(" last=");
    for (unsigned i = (n > 8 ? n - 8 : 0); i < n; ++i) { Console.print(seq[i]); Console.print(i + 1 < n ? "," : ""); }
    Console.println();
    return;
  }
  long sum = 0; int lo = 4096, hi = -1;
  for (unsigned i = 0; i < n; ++i) { const int v = analogRead((pin_size_t)pin); sum += v; if (v < lo) lo = v; if (v > hi) hi = v; }
  Console.print("ADC "); Console.print(name); Console.print(" n="); Console.print(n); Console.print(" min="); Console.print(lo);
  Console.print(" max="); Console.print(hi); Console.print(" mean="); Console.println((long)(sum / (long)n));
}
