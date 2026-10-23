# I2CRegisterRead

[日本語](README.ja.md)

Select a register, then read a fixed-size block while checking both transaction results. Start with a second CH32 running [I2CSlaveRegister](../I2CSlaveRegister/README.md); no particular sensor module is required.

## Connections and operation

Connect the boards' SDA pins together, SCL pins together, and common GND. Fit external pull-ups from SDA and SCL to their compatible I/O supply; account for any pull-ups already fitted. Do not connect two independently powered supply outputs together. Ensure the selected I2C route does not share a pad with either board's diagnostic UART; change pins to another valid route when necessary.

Upload the slave example to one board and this example to the master. Once per second the master reads three bytes from address `0x42`, register zero. Expect `32`, a changing write counter, and the writable register value (initially zero), in hexadecimal. The counter wraps after 255 and counts successful STOP-delivered writes, including register selection.

## API and adaptation

`begin(SDA, SCL)` can fail for an invalid or occupied pin route. `beginTransmission()` / `write()` buffer the register selector; `endTransmission()` sends it and returns an error code. `requestFrom()` then returns the actual received length. Never parse a short reply as a complete register block; the example drains it and tries again at the next interval.

`STOP_AFTER_REGISTER = true` is required for the paired slave: this core delivers its `onReceive()` callback at STOP. For an external device that requires a repeated START, set it to `false`: `endTransmission(false)` retains the bus and the following `requestFrom()` issues the next START, ending the read with STOP. Consult that device's register protocol before changing address, selector width, byte order or count. The three-byte map is this example's protocol, not a generic sensor format.

A NACK may mean no slave, wrong address or an unpowered device. For timeout check pull-ups and bus levels; see [I2CTimeoutRecovery](../I2CTimeoutRecovery/README.md) and the [Wire API](../../README.md). An address here is seven bits, not an already shifted address with a read/write bit.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
