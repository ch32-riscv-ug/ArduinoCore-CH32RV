# USBPD — ask a USB PD charger for a voltage (sink)

```cpp
#include <USBPD.h>

if (USBPD.begin()) {
    while (!USBPD.ready()) { }
    USBPD.request(9000);                    // 9 V please (millivolts!)
}
void loop() { USBPD.maintain(); }           // keeps a PPS contract alive
```

The hardware driver is implemented for CH32X035/X033 and supports fixed and
PPS contracts. On the other series with a USBPD block, `begin()` returns false
because their register and CC-pin definitions are not implemented (see `usbpd_hw.h`).

Every call (`ready()`, `connected()`, `request()`, `maintain()`) moves the
driver along, so waiting on `ready()` alone reaches a contract. Started
under a live contract (a reset, a re-flash), it gets the charger's
capabilities back with Soft_Reset, then Hard Reset. It retransmits, follows
the spec's timers, answers Get_Sink_Cap, says Not_Supported to the rest, and
re-requests a PPS contract every 5 s while `loop()` calls `maintain()`.
`contractProfile()` and `pps()` say what the contract is on.

The API also compiles for CH32L103/M103, CH32V205, CH32X315, CH32H417 and
CH32M030, but those series have no hardware definition in this library.

Everything is millivolts and milliamps. `request()` matches fixed profiles
exactly and PPS ranges inclusively (20 mV steps, truncated); it prefers a
fixed profile because a PPS contract dies unless re-requested every few
seconds (`maintain()` does that from `loop()`). Battery and variable
profiles are listed but never requested. See README.ja.md for the full API
and design notes.
