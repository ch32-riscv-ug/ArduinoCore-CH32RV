# Mouse

日本語: [README.ja.md](README.ja.md)

A USB mouse generating relative movement, a wheel event and a left-button press/release without external sensors.

## Hardware and connections

A PC and UART monitor. The sketch moves and clicks automatically, so use a test screen.

## Steps and expected results

1. Movement steps right 30, down 30, left 30, up 30 occur about one second apart.
2. A +1 wheel step follows, then a roughly 50 ms left-button press/release.
3. The six steps repeat; UART reports transmission failures as `MOUSE_FAILED`.

## Settings and limits

`move()` takes relative movement, not absolute screen coordinates. Pointer acceleration affects visible travel. When replacing the sequence with buttons, send both press and release states.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `move(x, y)` | Sends signed relative movement. Split movements exceeding the argument type's range. |
| `wheel(amount)` | Sends a relative wheel amount; visible scrolling depends on host settings. |
| `press(button)` / `release(button)` | Hold and release a button selected by its constant. Both operations matter. |
| `click(button)` | Combines a press and release. |

For sensor input, retain unsent movement until a send succeeds. Casting an out-of-range value can reverse its sign. Decide how to retry failed reports, including their button state.

The sample's `delay(50)` demonstrates a held button. To handle other inputs concurrently, schedule its release with `millis()` and return to `loop()`.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[Mouse.ino](Mouse.ino)
