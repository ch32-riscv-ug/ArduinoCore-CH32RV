# I2CSlaveRegister

[日本語](README.ja.md)

Expose a three-byte register map for [I2CRegisterRead](../I2CRegisterRead/README.md) or another I2C master. The callbacks do only small bounded operations; foreground code prints diagnostic snapshots.

## Connections and register map

Use the two-board wiring described in the master guide, including external pull-ups and compatible I/O levels. The slave address is seven-bit `0x42`.

| Register | Read | Write |
| --- | --- | --- |
| 0 | Identification value `0x32` | Ignored |
| 1 | STOP-delivered write count modulo 256 | Ignored |
| 2 | User byte, initially zero | Stores the byte |

A write's first byte selects a register. Further bytes write consecutive registers; read-only or out-of-range addresses are ignored. A read returns the remaining registers from the selector without advancing it between transactions. The master must select the register with a write ending in STOP before reading. For example, `02 7B` written with STOP stores `0x7B` in register 2; a subsequent read returns that value. Select zero again to read the entire block. Out-of-range reads, or bytes requested beyond the prepared reply, return `FF`.

For a master-side write to register 2, check the result of this transaction:

```cpp
Wire.beginTransmission(0x42);
Wire.write((uint8_t)2);
Wire.write((uint8_t)0x7B);
const uint8_t error = Wire.endTransmission();
```

## Operation and API

Start the slave, then the master. The slave prints its receive count and stored user value once per second. `Wire.begin(address, SDA, SCL, frequency)` selects slave mode and reports startup success; the master supplies the bus clock. `onReceive(count)` reads the master's write at STOP. `onRequest()` prepares reply bytes using `Wire.write()`.

Both callbacks run in interrupt context. They must not call `Serial`, wait, allocate dynamic storage or start another bus transaction. The small local reply array is safe here because `Wire.write()` copies it into Wire's reply buffer before returning. `loop()` briefly disables interrupts to copy related shared counters and restores them before printing. This critical section is used only from normal foreground execution.

The paired example deliberately uses STOP. This core's slave callback does not deliver the selected register before a repeated-START read, so do not use it as a test slave for that sequence. Keep messages within Wire's configured capacity. For callbacks, timeout behavior and limits, see [Wire](../../README.md).

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
