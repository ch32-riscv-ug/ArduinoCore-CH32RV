# UsbBringUpCheck

[English](README.md)

新しい板やケーブルで最初に動かす、USB デバイスの立ち上げの確認です（EspUsbDevice の EspUsbDeviceBringUpCheck と同じ役割）。
「デバイスが始まった」（`BEGIN ok`）と「ホストが設定した」（`MOUNTED`）を分けて示し、休止と再開を追い、ホストからデバイスへの
向き（キーボードのロックの LED）を確かめます。10 秒たっても列挙されなければ確認項目を出します。HID のキーボードを登録しますが、
キーは打ちません。

## 必要なものと配線

- USB デバイスのある石と、PC への USB の線（石の USB のコネクタに）。
- 115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

```
=== CH32RVUsbDevice bring-up check ===
CORE ch32rv 0.9.0  F_CPU 48000000 Hz
BEGIN ok
DESCRIPTOR config_bytes=34
VID_PID 1209:0001
Now connect the USB connector to a host and watch for MOUNTED.
MOUNTED t=812ms speed=full
HOST_OUTPUT_REPORT leds=0x02 num=0 caps=1 scroll=0
STATUS mounted=1 suspended=0 t=5000ms leds=0x02
```

- `BEGIN failed` は板の側の問題です（ホストはまだ関わっていない）。表示される名前で原因を絞ります。
- `BEGIN ok` なのに `MOUNTED` が出ないと、10 秒後に `CHECKLIST` が出ます（ケーブル、コネクタ、CPU Clock のメニュー、電源、
  ホストのログ、記述子）。
- PC のキーボードで CapsLock を押すと `HOST_OUTPUT_REPORT` が出ます。これでホストからデバイスへの向きも動いています。
- PC がスリープすると `SUSPENDED`、起きると `RESUMED`。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `device.begin(config)` / `lastErrorName()` | 始まったか、だめならその理由。 |
| `device.ready()` | ホストが設定を選んだ（列挙が終わった）。 |
| `device.speed()` / `device.suspended()` | 速さと休止。休止の間も `ready()` は true。 |
| `keyboard.onOutputReport()` / `ledState()` | ホストが送るロックの LED。ホストからデバイスへの向きの確認。 |

記述子そのものを見るのは [DescriptorDump](../DescriptorDump/README.ja.md) です。
