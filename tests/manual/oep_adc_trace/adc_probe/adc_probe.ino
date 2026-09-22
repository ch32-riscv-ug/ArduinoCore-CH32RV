// analogRead on request: ADC <pin_name> [n]  -> "ADC <name> n=<n> min=<> max=<> mean=<>" (n samples, default 16)
// ADCSEQ <pin_name> [n]: first/last 8 of n back-to-back conversions (settling / decay evidence)
// CH <channel> [n]: raw regular-channel conversion after analogRead(PA0) set the ADC up (15 = VREFINT)
#define TC_CMD_MAX 64
#include "testcmd.h"
#include "ch32_registers.h"   // raw ADC register access for the CH command
struct Named { const char *name; uint8_t pin; };
static const Named kPins[] = {{"PA0", PA0}, {"PA1", PA1}, {"PA2", PA2}, {"PA3", PA3}, {"PA4", PA4}, {"PA5", PA5}, {"PA6", PA6}, {"PA7", PA7}};
void setup() { tc_begin("adc_probe"); }
void loop() {
  const char *cmd = tc_ready();
  if (!cmd) return;
  char verb[8] = {0}, name[6] = {0}; unsigned n = 16;
  if (sscanf(cmd, "%7s %5s %u", verb, name, &n) < 2 || (strcmp(verb, "ADC") && strcmp(verb, "ADCSEQ") && strcmp(verb, "CH"))) { Serial.print("ERR "); Serial.println(cmd); return; }
  if (!strcmp(verb, "CH")) {   // raw channel: reuse the core's ADC setup, then point RSQR3 at the channel
    const unsigned ch = (unsigned)atoi(name); if (ch > 15) { Serial.println("ERR ch"); return; }
    (void)analogRead(PA0);
    if (n > 1000) n = 1000;
    long sum = 0; int lo = 4096, hi = -1, first = -1, last = -1;
    for (unsigned i = 0; i < n; ++i) {
      CH32_ADC_RSQR1 = 0; CH32_ADC_RSQR3 = ch; CH32_ADC_CTLR2 |= CH32_ADC_CTLR2_SWSTART;
      while ((CH32_ADC_STATR & CH32_ADC_STATR_EOC) == 0u) {}
      const int v = (int)(CH32_ADC_RDATAR & 0xFFFu) >> 2;   // 10-bit like analogRead()
      sum += v; if (v < lo) lo = v; if (v > hi) hi = v; if (i == 0) first = v; last = v;
    }
    Serial.print("CH "); Serial.print(ch); Serial.print(" n="); Serial.print(n); Serial.print(" min="); Serial.print(lo);
    Serial.print(" max="); Serial.print(hi); Serial.print(" mean="); Serial.print((long)(sum / (long)n));
    Serial.print(" first="); Serial.print(first); Serial.print(" last="); Serial.println(last);
    return;
  }
  int pin = -1; for (const Named &p : kPins) if (!strcmp(p.name, name)) pin = p.pin;
  if (pin < 0) { Serial.print("ERR pin "); Serial.println(name); return; }
  pinMode((pin_size_t)pin, INPUT);
  if (!strcmp(verb, "ADCSEQ")) {   // print the first 8 and the last 8 of n back-to-back conversions
    static int seq[1000]; if (n > 1000) n = 1000;
    for (unsigned i = 0; i < n; ++i) seq[i] = analogRead((pin_size_t)pin);
    Serial.print("ADCSEQ "); Serial.print(name); Serial.print(" first=");
    for (unsigned i = 0; i < 8 && i < n; ++i) { Serial.print(seq[i]); Serial.print(i + 1 < 8 ? "," : ""); }
    Serial.print(" last=");
    for (unsigned i = (n > 8 ? n - 8 : 0); i < n; ++i) { Serial.print(seq[i]); Serial.print(i + 1 < n ? "," : ""); }
    Serial.println();
    return;
  }
  long sum = 0; int lo = 4096, hi = -1;
  for (unsigned i = 0; i < n; ++i) { const int v = analogRead((pin_size_t)pin); sum += v; if (v < lo) lo = v; if (v > hi) hi = v; }
  Serial.print("ADC "); Serial.print(name); Serial.print(" n="); Serial.print(n); Serial.print(" min="); Serial.print(lo);
  Serial.print(" max="); Serial.print(hi); Serial.print(" mean="); Serial.println((long)(sum / (long)n));
}
