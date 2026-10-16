# ListProfiles

日本語: [README.ja.md](README.ja.md)

Lists charger PDOs and requests 9 V. This actively changes VBUS voltage rather than only inspecting capabilities.

## Hardware and connections

CH32X035/X033 PD CC wiring, a PD charger/cable and a UART monitor. Board power circuitry and loads on VBUS must support the requested 9 V.

## Steps and expected results

1. Open UART at 115200 baud, connect a PD charger and start.
2. Inspect kind, voltage range and current under `the charger offers:`.
3. A successful request prints `now at 9000 mV...` and the profile index.
4. Failure prints `this charger has no 9 V`; this simplified message can also include negotiation/request failure.

## Settings and limits

Initial contract waiting is limited to about five seconds. A Type-C-only source may be connected without ready. request(9000) prefers fixed 9 V, with a compatible PPS range also eligible. Voltage/current outputs are negotiated values, not measurements.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbPd API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[ListProfiles.ino](ListProfiles.ino)
