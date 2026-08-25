# USBPD — ask a USB PD charger for a voltage (sink)

```cpp
#include <USBPD.h>

if (USBPD.begin()) {
    while (!USBPD.ready()) { }
    USBPD.request(9000);                    // 9 V please (millivolts!)
}
void loop() { USBPD.maintain(); }           // keeps a PPS contract alive
```

**Status: the frame logic (capability parsing, profile choice, request
encoding) is implemented and tested on host and on target. The hardware
driver underneath is not written yet, so `begin()` currently returns false
everywhere.** A sketch that checks the return value will work unchanged the
day the driver lands.

Targets the USBPD block found on CH32X035/X033, CH32L103/M103, CH32V205,
CH32X315, CH32H417 and CH32M030 - not only the X035.

Everything is millivolts and milliamps. `request()` matches fixed profiles
exactly and PPS ranges inclusively (20 mV steps, truncated); it prefers a
fixed profile because a PPS contract dies unless re-requested every few
seconds (`maintain()` does that from `loop()`). Battery and variable
profiles are listed but never requested. See README.ja.md for the full API
and design notes.
