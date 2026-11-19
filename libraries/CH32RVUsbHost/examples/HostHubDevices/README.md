# HostHubDevices

[日本語](README.ja.md)

Devices behind a hub. Each connection or disconnection lists the whole tree: every device with its address, the hub
and port it hangs on, its speed and whether a driver took it, and a hub's port count. Unplugging one device leaves the
others listed and working.

## What you need and the wiring

- A part with a USB host port (USBFS), and 5 V out to the devices (VBUS). Add up the hub's and the devices' current and
  check the 5 V supply gives that much.
- A USB hub (a self-powered one eases the current) and two devices: a keyboard, a mouse, a USB stick and the like.
- A UART monitor at 115200 bps (`Serial`).

## What to do and what to expect

With a hub plugged in and a keyboard and a mouse behind it, the output is like this (addresses and IDs differ by
device):

```
connected: dev 1 1a40:0101
1 device(s)
  dev 1 1a40:0101 full speed, on the root port, a hub with 4 ports (USB 2.0 Hub)
connected: dev 2 046d:c31c
2 device(s)
  dev 1 1a40:0101 full speed, on the root port, a hub with 4 ports (USB 2.0 Hub)
  dev 2 046d:c31c low speed, on hub 1 port 2, driver attached (USB Keyboard)
```

Unplugging the mouse prints `disconnected:` and then the remaining devices. The keyboard keeps working.

## The API and the flow

| API | Its use here |
|---|---|
| `onDeviceConnected()` / `onDeviceDisconnected()` | Connection and disconnection notices: a flag is set and `loop()` lists again. |
| `getDevices()` | The devices connected now. `parentAddress` (0 for the root port) and `portId` give the tree's shape. |
| `getHubInfo()` | A hub's port count and the like. |
| `supported` / `isHub` / `speed` | Whether a driver took it, whether it is a hub, its speed (low / full / high / super). |

The callbacks run from `deliverEvents()` in `loop()`.
