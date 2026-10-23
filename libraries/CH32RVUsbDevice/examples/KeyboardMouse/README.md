# KeyboardMouse

日本語: [README.ja.md](README.ja.md)

Keyboard and mouse functions sharing one HID interface, distinguished by report ID.

## Hardware and connections

A PC text editor and UART monitor. Text, movement and clicks are generated automatically.

## Steps and expected results

1. About every three seconds the sketch types `K`, moves right by 40 and clicks left.
2. Observe both functions using an editor and a HID viewer.
3. UART distinguishes `KEY_FAILED`, `MOVE_FAILED` and `CLICK_FAILED`.

## Settings and limits

Declare both functions on the same `device` and call `begin()` once. The combined HID uses report IDs; its report layout differs from a standalone boot keyboard.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `keyboard(device)` / `mouse(device)` | Add both HID functions to the same device. |
| `keyboard.write("K")` | Sends a key press and release to the focused PC application. |
| `mouse.move(40, 0)` | Sends relative rightward movement, not an absolute screen coordinate. |
| `mouse.click(...)` | Sends a button press and release. |

The functions share an HID interface and are distinguished by Report ID. Raw-report applications must inspect the report descriptor rather than infer functions from interface count alone.

Movement and a click follow keyboard input and may change focus. For physical controls, replace periodic actions with input changes and check each API's boolean result.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[KeyboardMouse.ino](KeyboardMouse.ino)
