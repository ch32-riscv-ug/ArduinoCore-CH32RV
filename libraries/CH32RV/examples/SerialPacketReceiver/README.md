# SerialPacketReceiver

[日本語](README.ja.md)

Read a small binary packet using a state machine and fixed storage. Packet boundaries need not match UART receive chunks.

## Packet format and operation

A packet is `A5 length payload... checksum`. Length is 1–16 bytes. Checksum is the XOR of the length and all payload bytes, excluding `A5`. For example, `A5 03 10 20 30 03` is valid; a payload may itself contain `A5` or zero. All numbers here are hexadecimal bytes, not ASCII text.

With a Python environment containing pyserial, replace the port name and run:

```python
import serial
with serial.Serial('/dev/ttyUSB0', 115200, timeout=2) as port:
    port.write(bytes.fromhex('A5 03 10 20 30 03'))
    port.flush()
    print(port.read(256).decode(errors='replace'))
```

If opening the port resets the board, wait for its startup before writing. A correct packet prints `Packet bytes: 10 20 30`. Send the same packet with a different last byte for `Checksum mismatch`, `A5 00` or `A5 11` for `Invalid length`, and `A5 03 10` followed by more than one second of silence for timeout. Two valid packets concatenated must produce two replies.

## API and adaptation

`available()` / `read()` feed one byte at a time into start, length, payload and checksum states. An invalid length is rejected before indexing the payload. Each loop processes at most 16 bytes. `millis()` tracks inactivity while a packet is incomplete, using subtraction across timer wraparound.

After an error the parser searches for a later `A5`; it cannot guarantee immediate resynchronization if corrupted lengths consume part of another packet. Stop transmission for longer than the timeout before retrying after a malformed partial packet. XOR detects some errors but is not a robust CRC or authentication. A real protocol may need escaping, sequence numbers and acknowledgments. Diagnostic output can block and sustained traffic can overflow the UART buffer; this is a framing example, not a throughput benchmark.

## Before running

Select your board and exact Part Number, then check the sketch's pins against its schematic. UART logging uses hardware `Serial` at 115200 baud, 8N1: connect adapter RX to board TX, adapter TX to board RX, and common GND. Use compatible I/O voltages. The board's USB connector does not automatically carry this UART.

The generated `sketch.yaml` supplies build profiles. A successful build does not verify wiring or hardware behavior. See [build rules](../../../../docs/examples-build-rules.ja.md) and [upload routes](../../../../docs/upload-routes.ja.md).
