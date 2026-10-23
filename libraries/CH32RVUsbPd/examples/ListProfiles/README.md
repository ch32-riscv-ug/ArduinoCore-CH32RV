# ListProfiles

日本語: [README.ja.md](README.ja.md)

Lists charger PDOs and requests 9 V. This actively changes VBUS voltage rather than only inspecting capabilities.

## Hardware and connections

A series with USBPD (CH32X035 and others; CC pads in the library README) PD CC wiring, a PD charger/cable and a UART monitor. Board power circuitry and loads on VBUS must support the requested 9 V.

## Steps and expected results

1. Open UART at 115200 baud, connect a PD charger and start.
2. Inspect kind, voltage range and current under `the charger offers:`.
3. A successful request prints `now at 9000 mV...` and the profile index.
4. Failure prints `this charger has no 9 V`; this simplified message can also include negotiation/request failure.

## Settings and limits

Initial contract waiting is limited to about five seconds. A Type-C-only source may be connected without ready. request(9000) prefers fixed 9 V, with a compatible PPS range also eligible. Voltage/current outputs are negotiated values, not measurements.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `USBPD.begin()` | Starts sink attach detection; success is not yet an explicit contract. |
| `connected()` / `ready()` | Distinguish Type-C attach from PD contract completion; these calls also advance the driver. |
| `profileCount()` / `profile(i)` | Read advertised PDOs using zero-based indices; check kind and field units. |
| `request(millivolts, milliamps)` | Waits for a voltage request result and returns success. Omitted current follows advertised capability. |
| `contractProfile()` / `voltage()` / `current()` | Inspect the selected contract rather than merely advertised maxima. |

`request(9)` requests 9 mV, not 9 V. Fixed PDOs require an exact voltage; PPS uses an advertised range. Use `requestProfile(index, mv, ma)` to select a particular PPS PDO.

Listing and requesting happen once during startup. For later connection/replacement, detect state changes in `loop()` and repeat them. The simple failure message does not distinguish every negotiation failure from missing capabilities.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbPd API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[ListProfiles.ino](ListProfiles.ino)
