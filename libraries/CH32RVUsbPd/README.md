# CH32RVUsbPd — ask a USB PD charger for a voltage (sink), or offer voltages (source)

[日本語](README.ja.md)

The predefined object is `USBPD`. Using it together with the USB device is
covered in [docs/usb.ja.md](../../docs/usb.ja.md) (Japanese).

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
void loop() { }                             // begin() keeps the link (and PPS) alive
```

This changes the VBUS voltage: the board's input circuitry and its loads must
take the voltage asked for. The first steps, the CC / UART wiring and how
success and failure show are in the [shared setup](examples/README.md).
Listing and requesting a charger's profiles is
[ListProfiles](examples/ListProfiles/README.md), stepping through them with a
button [CycleVoltages](examples/CycleVoltages/README.md), and the frame of a
source's supply control [PowerSource](examples/PowerSource/README.md).

## What is implemented

| Layer | State |
|---|---|
| Parsing Source_Capabilities, choosing a profile, building the Request | implemented |
| The hardware driver (CC detection, BMC send and receive, GoodCRC, the sink's state machine) | implemented on the series below, fixed and PPS contracts |
| X033 | `begin()` is `false` (no CC pad) |

| Series | CC1 / CC2 | Pads | Status |
|---|---|---|---|
| CH32X035 | PC14 / PC15 | input | sink checked on hardware, source built only |
| CH32L103 / M103 | PB6 / PB7 | input | built only |
| CH32V205 | PA0 / PA1 | AF7 | built only (lots whose 5th digit is 0 have no USBPD) |
| CH32X305 / X315 | PD4 / PD5 | AF2 | built only |
| CH32H415 / H416 / H417 | PB3 / PB4 | AF4 | built only |
| CH32M030 | PA0 / PA1 (USBPD0) | input | built only. USBPD1 (PA2 / PA3) is not used. Its comparator bits (CC_CE, CC_CVS) take other values |

The block is the same on every series; where it sits, the CC pads and the
PHY's AFIO bits differ (`usbpd_hw.h`). The BMC clock is derived from F_CPU (at
the 8 MHz CPU Clock it sends 2.5 % fast, within the spec; not checked on
hardware).

What the driver does by itself (after USB PD R3.1):

- **`begin()` is all it takes.** `begin()` attaches the driver as a system
  service: `delay()` and the return of `loop()` run attach detection, the
  spec's timers, retransmission and PPS upkeep. There is no `maintain()` to
  put in `loop()`. `ready()` / `connected()` / `request()` move the state on
  too, so a loop waiting on `ready()` alone reaches a contract.
- It answers Source_Capabilities with a Request (the spec requires it): the
  profile the sketch last got, when **the same PDO (to the raw value)** is at
  the same position; profile 0 (5 V) otherwise (another charger, a changed
  list, unknown). A detach forgets the choice.
- **Started under a live contract** (a reset, right after an upload): no
  Source_Capabilities comes, so after tTypeCSinkWaitCap (620 ms) it sends
  Soft_Reset (VBUS stays up), then Hard Reset (VBUS drops and comes back at
  5 V; a board powered from VBUS alone restarts), up to twice. A charger that
  does not answer is Type-C only: `connected()` but never `ready()`.
- A message with no GoodCRC is sent twice more; a duplicate is ignored; a
  response timer running out (Accept 30 ms, PS_RDY 550 ms) means Soft or Hard
  Reset.
- Get_Sink_Cap is answered with a 5 V Sink_Capabilities, anything not
  implemented with Not_Supported (PD 3.0) / Reject (PD 2.0).
- **A PPS contract is re-requested every 5 s** (a charger drops it after 10 s
  of silence). The system service does it, so `delay(30000)` keeps it; a
  `loop()` or callback that does not return for seconds loses it.
- CC staying below vRd-Connect is a detach (meaningful only on a board not
  powered from the same VBUS).

## API

Units are **always mV and mA**. `request(9)` asks for 9 mV, and fails cleanly
since no profile has it.

| | |
|---|---|
| `begin()` / `end()` | Start and stop. `false` on a part without the USBPD block |
| `connected()` | A supply is attached on CC |
| `ready()` | An explicit contract is in place (PS_RDY); `profile()` and `voltage()` are live from then |
| `profileCount()` / `profile(i)` | The charger's advertised profiles. `[0]` is always 5 V fixed, by the spec |
| `request(mV, mA=0)` | Ask for a voltage. Fixed profiles match **exactly**, PPS ranges inclusively (20 mV steps, truncated). `mA=0` is "up to that profile's limit" |
| `requestProfile(i, mV=0, mA=0)` | Name a profile (to use PPS where a fixed profile has the same voltage) |
| `voltage()` / `current()` | **The contract's values** (not measured), in the Request's steps (PPS: 20 mV / 50 mA, truncated) |
| `contractProfile()` | The contract's profile number, `-1` without one |
| `pps()` | The contract is PPS (the system service re-requests it every 5 s) |
| `maintain()` | Moves the driver on once. **Not needed**: the system service `begin()` attached does the same in `delay()` and on the return of `loop()` |

`CH32RVUsbPdProfile`'s fields carry their units: `kind`
(`CH32RVUsbPdSupply::Fixed` / `Pps` / `Battery` / `Variable` / `Unknown`;
`ch32rv_usbpd_supply_name()` names it), `min_mv` / `max_mv` (equal for
fixed), `max_ma`, `max_mw` (battery only) and `raw`. The sink's
`USBPD.profile()` and the source's `USBPDSource.profile()` both return it.

## Design decisions

- **`request()` prefers a fixed profile.** A PPS contract dies unless
  re-requested every few seconds (the spec's tPPSTimeout, 10 s), so while a
  fixed profile has the same voltage it picks that one, which a sketch stopped
  in `delay(30000)` keeps. Name a PPS profile with `requestProfile()` to use it.
- **No voltage in between is rounded.** `request(8000)` from a 5/9/12 V
  charger (without PPS) is `false`; there is no "9 V is close enough".
- **Battery and variable profiles are listed only**, never requested.
- The series with a USBPD block are X035, L103/M103, V205, X305/X315, H41x
  and M030 (X033 has no CC pad). Where the block sits, its clock enable, IRQ
  and CC pads differ by series; the library has the definitions for X035,
  L103/M103, V205, X305/X315, H41x and M030 (USBPD0). On X033 `begin()`
  returns `false`.

## Why the logic and the driver are apart

What breaks easily in USB PD is the bit-field layout and the unit conversions
(10 mA / 50 mA / 50 mV / 100 mV / 20 mV / 250 mW). They need no wiring, and
put where only hardware can check them, they are the least checked. So
`pd_frames.c` is **pure functions that know neither registers nor Arduino.h**,
and the same checks run on the host (ctypes) and on the chip (rv32ec).

The layout is the USB PD R3.1 spec's, cross-checked against WCH EVT's
`USBPD_SNK` and wagiminator's `CH32X035-USB-PD-Adapter` (CC BY-SA) **for
reference only**. No code was brought in.

## Board notes

Keep the CC lines short. On the WeAct board, a long lead from CC to a probe
pin left not one message decodable under load.

## Source (`USBPDSource`)

The same library also makes the board a USB PD source, the side that supplies
power (the same series). Switching the supply is the board's job, through two
callbacks. **Both are required**; without them `begin()` fails
(`lastErrorName()` says why). Whether the voltage really appears is outside
this library.

The callbacks below show the shape only: printing or `return true` alone does
not switch the supply. Replace them with real control of the VBUS switch and
the regulator, returning true once the state asked for is reached.

```cpp
bool vbus(bool on) { /* load switch */ return true; }
bool setVoltage(uint16_t mv, uint16_t ma, bool pps) { /* regulator */ return true; }

void setup() {
    USBPDSource.setRpCurrent(CH32RVUsbPdRpCurrent::Current3A0);
    USBPDSource.addFixed(5000, 3000);       // the first profile must be 5 V fixed
    USBPDSource.addFixed(9000, 2000);
    USBPDSource.addPps(3300, 11000, 2000);
    USBPDSource.onVbus(vbus);
    USBPDSource.onSetVoltage(setVoltage);
    USBPDSource.begin();
}
void loop() { }        // begin() attached the driver
```

- It presents Rp, detects the sink's Rd (tCCDebounce), picks the connected CC
  line and switches VBUS on with `onVbus(true)`.
- It sends Source_Capabilities every tTypeCSendSourceCap until a GoodCRC comes
  back (giving up after nCapsCount leaves it Type-C only: `attached()` but
  never `ready()`).
- A Request is checked against what was offered (position, current, the PPS
  range): Reject when outside it; otherwise Accept, `onSetVoltage()` after
  tSrcTransition, and PS_RDY.
- A callback that returns false: VBUS false leaves VBUS off (`VbusFailed`
  until the sink detaches); voltage false ends in a Hard Reset back to 5 V
  (`VoltageFailed`). Accept has gone out by then, so it cannot be a Reject.
- It answers Get_Source_Cap and Soft_Reset, and recovers from a Hard Reset by
  dropping VBUS for tSrcRecover. A PPS contract whose Requests stop for 15 s
  ends in a Hard Reset. Anything not implemented gets Not_Supported (PD 3.0) /
  Reject (PD 2.0).
- A detach (Rd gone) calls `onVbus(false)`.
- The sink and the source share the USBPD block, so only one runs at a time
  (the other is `Busy`).
- A board used as a source must not have Rd on CC. On the WeAct CH32X035 board,
  remove R4 / R5 (5.1 kΩ).
