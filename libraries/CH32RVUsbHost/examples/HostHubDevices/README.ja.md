# HostHubDevices

[English](README.md)

ハブの先の機器を扱います。つないだり抜いたりするたびに木の全体を表示します。機器ごとにアドレス、つながっているハブと口、
速さ、ドライバーが付いたかを出し、ハブは口の数も出します。1 台を抜いても、ほかは一覧に残って動き続けます。

## 必要なものと配線

- USB ホストの口（USBFS）のある石。機器へ 5 V を出す配線（VBUS）。ハブと機器の消費電流を足して、5 V の出せる量を確かめます。
- USB のハブ（セルフパワーなら電流の心配が減ります）と、キーボード・マウス・USB メモリなどの機器を 2 台。
- 115200 bps の UART モニタ（`Serial`）。

## 操作と期待する結果

ハブをつなぎ、その先にキーボードとマウスをつなぐと、次のように出ます（アドレスや ID は機器で違います）。

```
connected: dev 1 1a40:0101
1 device(s)
  dev 1 1a40:0101 full speed, on the root port, a hub with 4 ports (USB 2.0 Hub)
connected: dev 2 046d:c31c
2 device(s)
  dev 1 1a40:0101 full speed, on the root port, a hub with 4 ports (USB 2.0 Hub)
  dev 2 046d:c31c low speed, on hub 1 port 2, driver attached (USB Keyboard)
```

マウスを抜くと `disconnected:` のあとに、残りの機器の一覧が出ます。キーボードはそのまま使えます。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `onDeviceConnected()` / `onDeviceDisconnected()` | つないだ・抜いたの通知。印を立て、`loop()` で一覧を出し直します。 |
| `getDevices()` | 今つながっている機器の一覧。`parentAddress`（0 は根の口）と `portId` で木の形が分かります。 |
| `getHubInfo()` | ハブの口の数など。 |
| `supported` / `isHub` / `speed` | ドライバーが付いたか、ハブか、速さ（low / full / high / super）。 |

通知の関数は `loop()` の `deliverEvents()` から呼ばれます。
