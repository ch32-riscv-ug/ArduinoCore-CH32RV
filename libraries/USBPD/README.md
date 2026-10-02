# USBPD — ask a USB PD charger for a voltage (sink)

```cpp
#include <USBPD.h>

if (USBPD.begin()) {
    while (!USBPD.ready()) { }
    USBPD.request(9000);                    // 9 V please (millivolts!)
}
void loop() { USBPD.maintain(); }           // keeps a PPS contract alive
```

**Status: implemented for CH32X035/X033 and verified against real chargers
(2026-10-02, WeAct CH32X035F8U6): a 5/9/12/15/20 V fixed one and a
5/9/12 V + 3.3-11 V PPS one - every fixed level, PPS at both ends and the
middle of its range held 20 s on `maintain()` alone, refusals, and a restart
while the charger still holds a contract (tests/bench/basic/pd_sink). On the
other five series with the block, `begin()` returns false until their
defines can be generated (see usbpd_hw.h).**

Every call (`ready()`, `connected()`, `request()`, `maintain()`) moves the
driver along, so waiting on `ready()` alone reaches a contract. Started
under a live contract (a reset, a re-flash), it gets the charger's
capabilities back with Soft_Reset, then Hard Reset. It retransmits, follows
the spec's timers, answers Get_Sink_Cap, says Not_Supported to the rest, and
re-requests a PPS contract every 5 s while `loop()` calls `maintain()`.
`contractProfile()` and `pps()` say what the contract is on.

Targets the USBPD block found on CH32X035/X033, CH32L103/M103, CH32V205,
CH32X315, CH32H417 and CH32M030 - not only the X035.

Everything is millivolts and milliamps. `request()` matches fixed profiles
exactly and PPS ranges inclusively (20 mV steps, truncated); it prefers a
fixed profile because a PPS contract dies unless re-requested every few
seconds (`maintain()` does that from `loop()`). Battery and variable
profiles are listed but never requested. See README.ja.md for the full API
and design notes.
