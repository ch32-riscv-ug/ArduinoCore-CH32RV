# PowerSource

日本語: [README.ja.md](README.ja.md)

A PD source protocol template with callbacks for a VBUS switch and adjustable supply. As supplied it only logs; it does not control a physical supply.

## Hardware and connections

CH32X035/X033 source CC circuitry, a current-limited supply, VBUS switch, programmable regulator and UART monitor. On WeAct X035 the sink Rd resistors R4/R5 must be removed for source operation.

## Steps and expected results

1. Replace vbus and setVoltage with real power-circuit control, returning true only after switching completes.
2. Change advertised 5 V/3 A, 9 V/2 A and PPS 3.3–11 V/2 A to actual circuit capabilities.
3. Connect a sink and inspect VBUS-on, voltage and contract logs.
4. Verify VBUS-off on detach and return to 5 V on reset/failure using the physical circuit.

## Settings and limits

The supplied callbacks always return true after printing. Implement real switching before connecting a sink: otherwise PS_RDY is sent without the requested voltage being applied. setRpCurrent does not make a supply capable of 3 A. USBPD sink and USBPDSource cannot own the PD block simultaneously.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbPd API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[PowerSource.ino](PowerSource.ino)
