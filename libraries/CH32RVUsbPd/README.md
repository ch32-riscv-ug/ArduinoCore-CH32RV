# CH32RVUsbPd — ask a USB PD charger for a voltage (sink), or offer voltages (source)

[日本語](README.ja.md)

Formerly the `USBPD` library (`USBPD.h`); the predefined object is still `USBPD`. The USB guide
(Japanese) is [docs/usb.ja.md](../../docs/usb.ja.md).

```cpp
#include <CH32RVUsbPd.h>

void setup() {
    if (!USBPD.begin()) {
        return;
    }
    const uint32_t start = millis();
    while (!USBPD.ready()) {
        if (millis() - start >= 5000) {
            return;                       // absent or non-PD source
        }
    }
    USBPD.request(9000);                   // 9 V; check the result in real code
}
void loop() { USBPD.maintain(); }           // keeps a PPS contract alive
```

This changes VBUS voltage; board input circuitry and loads must tolerate it.
See [shared setup](examples/README.md),
[ListProfiles](examples/ListProfiles/README.md),
[CycleVoltages](examples/CycleVoltages/README.md) and
[PowerSource](examples/PowerSource/README.md) for wiring, steps and results.

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

## Source (`USBPDSource`)

The same library also makes the board a USB PD source (CH32X035/X033). The
board switches its own supply through two callbacks; both are required and
`begin()` fails without them (`lastErrorName()` says why). Whether the supply
really reaches the voltage is the board's job.

The callbacks below show signatures only. Replace them with actual VBUS and
regulator control, returning true only when switching has completed. Printing
or returning true alone does not change the physical supply.

```cpp
bool vbus(bool on) { /* load switch */ return true; }
bool setVoltage(uint16_t mv, uint16_t ma, bool pps) { /* regulator */ return true; }

void setup() {
    USBPDSource.setRpCurrent(PD_RP_3A0);
    USBPDSource.addFixed(5000, 3000);       // the first profile must be 5 V fixed
    USBPDSource.addFixed(9000, 2000);
    USBPDSource.addPps(3300, 11000, 2000);
    USBPDSource.onVbus(vbus);
    USBPDSource.onSetVoltage(setVoltage);
    USBPDSource.begin();
}
void loop() { USBPDSource.maintain(); }
```

It presents Rp, debounces a sink's Rd and picks its CC line, switches VBUS
on, sends Source_Capabilities until acknowledged, checks each Request against
the offer (Reject if outside it), and after Accept calls `onSetVoltage()` and
sends PS_RDY. A callback that returns false: VBUS false leaves the sink
unpowered until it detaches; voltage false ends in a Hard Reset back to 5 V.
It also answers Get_Source_Cap and Soft_Reset, recovers from Hard Reset
(VBUS off for tSrcRecover), drops a PPS contract the sink stops renewing
(15 s), and switches VBUS off on detach. Sink and source share the USBPD
block, so only one runs at a time (`Busy`). A board used as a source must not
have Rd resistors on CC (the WeAct CH32X035 board: remove R4/R5).
