# I2CTimeoutRecovery

[日本語](README.ja.md)

Distinguish an address NACK from a timed-out bus, then attempt line recovery only when explicitly requested. Use a known I2C bus you control; [I2CSlaveRegister](../I2CSlaveRegister/README.md) can provide a responding address.

## Connections and operation

Use compatible I/O voltages, common GND and external SDA/SCL pull-ups. Check that SDA/SCL and UART do not share the same pads. Start with the healthy bus.

- Send `p` to probe `0x42`. With the paired slave, expect result zero and a clear timeout flag.
- With no device at that address but lines correctly pulled up, expect an address NACK (code 2), not a timeout.
- If the bus is already stuck low, probing can return timeout (code 5) and set the flag. Do not create a fault by shorting a driven pin to a supply.
- Send `r` only when bus recovery is appropriate for every device. It reports whether both lines are released. Then send `p` again. A released bus does not prove the target is present or its application state is correct.

## API and adaptation

`setWireTimeout()` takes microseconds. `getWireTimeoutFlag()` is sticky: clear it before the operation being inspected, then read it afterwards. A NACK and timeout require different troubleshooting; missing pull-ups and a device retaining SDA are possible timeout causes.

`clearBus()` emits recovery clocks and a STOP. These reach all connected devices. It cannot repair a hard short, incorrect power or a device continuously holding a line. The sample never retries recovery automatically. The timeout caps individual driver waits rather than guaranteeing the complete transaction finishes within that exact duration. See [Wire](../../README.md) for error codes and recovery semantics.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
