# ReconfigureVendor

[English](README.md)

動いている途中で USB デバイスを止め（`device.end()`）、別のバッファの大きさで始め直します。UART に `r` を送ると、ベンダーの
機能の送受信の輪を 128 と 2048 バイトで切り替えます。その間ベンダーの bulk のデータは捨てずにエコーします（EspUsbDevice の
ReconfigureVendor と同じ）。

## 必要なものと配線

USB デバイスのある石と、PC への USB の線。115200 bps の UART モニタ（`RX` / `TX`、`r` もここから送る）。PC 側でベンダーの
bulk を送受信する道具（[USBVendor](../USBVendor/README.ja.md) の PyUSB の手順など）。

## 操作と期待する結果

起動すると `Started: RX=127 TX=127` が出ます。`r` を送ると、PC には機器が抜けて戻ったように見え（列挙のやり直し）、
`Started: RX=2047 TX=2047` が出ます。もう一度 `r` で 128 に戻ります。PC から送ったベンダーのデータは、どちらの大きさでも
そのまま返ります。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `device.end()` | デバイスを止める。ホストには切断。輪に残っていたデータは捨てる。 |
| `vendor.configure(buffers)` | 輪の大きさ。止まっている間だけ効く。 |
| `device.begin()` | 新しい設定で始め直す。 |
| `vendor.readCapacity()` / `writeCapacity()` | 実際の輪の大きさ（1 バイトは空きの印に使う）。 |

CH32H417 の SuperSpeed で確かめました（2026-11-20。試験用の写しで、UART の `r` の代わりに 4 秒ごとに切り替え）: どちらの大きさでも、
列挙のやり直しのたびに 1000 バイトがそのまま戻りました。SuperSpeed では 128 バイトの受信の輪は 1 パケット（1024）に広がります
（それより小さいと、何も受けられませんでした）。
