# UsbConsole

[English](README.md)

ホストのアプリが思ったように反応しないときの道具です（EspUsbDevice の EspUsbDeviceConsole と同じ役割）。ハードウェアの UART の
端末から HID（キーボード・マウス・生の報告）とベンダーの転送を 1 つずつ送り、ホストから来るもの（HID の出力と protocol、
ベンダーの制御の要求、bulk OUT）を全部表示します。

## 必要なものと配線

- USB デバイスのある石と、試すホストへの USB の線。V203 では 32 KB の 98 % を使います（V203 の大きい品か、ほかの系列を勧めます）。
- 115200 bps・8N1 の端末を `RX` / `TX` に（コマンドもここで打つ）。

## 操作と期待する結果

| コマンド | すること |
|---|---|
| `help` / `state` | 一覧 / 列挙・休止・LED・HID の protocol・ベンダーの状態 |
| `text <string>` | キー入力として送る（ホストで前に出ている窓に入る） |
| `key <usage> [modifiers]` / `hold ...` / `release` | キーを 1 回押す / 押したまま / 放す（usage は HID の番号、0x04 が a） |
| `mouse <dx> <dy> [buttons] [wheel]` / `click <1\|2\|4>` | マウスの移動 / クリック |
| `report <id> <hex...>` | HID の instance 0 に生の報告（ID は含めない） |
| `vendor <hex...>` / `vendortext <string>` | ベンダーの bulk IN に送る |

ホストから来たものは `HOST_` で始まる行で出ます。

```
MOUNTED
HOST_HID_PROTOCOL instance=0 protocol=report
key 0x04
SEND key usage=0x04 modifiers=0x00 ok=1
HOST_HID_OUTPUT leds=0x02 num=0 caps=1 scroll=0
HOST_VENDOR_CONTROL stage=1 type=0xc0 request=0x01 value=0x0000 index=0x0000 length=17 actual=0
HOST_VENDOR_OUT 5 68 65 6c 6c 6f
```

## API と処理の流れ

- 送る側: `keyboard.tapUsage()` / `pressUsage()` / `releaseAll()` / `write()`、`mouse.move()` / `click()`、
  `device.sendHidReport()`、`vendor.write()`。
- 受ける側: `keyboard.onOutputReport()` / `onProtocol()`、`vendor.onRxData()`（bulk OUT をそのまま渡す）、
  `vendor.onControlRequest()`（どの要求にも答えるので、ホストには stall でなく成功に見える。調べるプロトコルを再現するなら
  `handleVendorControl()` を変える）。
- 通知の関数は `loop()` の `deliverEvents()` から呼ばれます。
