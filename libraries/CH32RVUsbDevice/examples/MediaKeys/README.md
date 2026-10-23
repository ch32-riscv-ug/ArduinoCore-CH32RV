# MediaKeys

日本語: [README.ja.md](README.ja.md)

Consumer media-key reports plus a declared System Control function.

## Hardware and connections

A PC with volume/media controls and a UART monitor. Volume and mute change automatically after connection.

## Steps and expected results

1. Volume up/down, mute, play/pause, next and previous cycle about every three seconds.
2. Compare UART `MEDIA_KEY` messages with host behavior.
3. Use a HID viewer if a player ignores a particular usage.

## Settings and limits

Power-off, standby and wake calls are commented out and are not sent by this sketch. Enable them only as part of an explicit input action in your application.

## API usage and adaptation

| API | Use in this example |
|---|---|
| `CH32RVUsbDeviceHidConsumerControl media(device)` | Adds media and volume controls. |
| `media.click(usage)` | Sends a usage press and release. Do not pass a character code. |
| `CH32RVUsbDeviceHidSystemControl systemControl(device)` | Adds separate HID controls affecting host power state. |

A successful send does not guarantee that the media player acted. Volume behavior also depends on the OS; track controls depend on application support. Use the boolean result to distinguish transmission failure from host behavior.

For buttons, debounce and call `click()` on the press transition. Calling it on every iteration while held repeats the action. Check the effects of individual System Control usages before enabling them.

## Related documentation

- [Shared setup and troubleshooting](../README.md)
- [CH32RVUsbDevice API and support](../../README.md)
- [USB guide (Japanese)](../../../../docs/usb.ja.md)

[MediaKeys.ino](MediaKeys.ino)
