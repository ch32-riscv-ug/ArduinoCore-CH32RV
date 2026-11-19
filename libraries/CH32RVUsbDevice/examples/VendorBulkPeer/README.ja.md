# VendorBulkPeer

[English](README.md)

1 バイトも失わないベンダーの bulk のエコーを、PC の [vendor_bulk_peer.py](vendor_bulk_peer.py)（PyUSB）と組んで動かします。
デバイスは bulk IN がその時に受けられる分だけ読むので、PC が読むのをやめるとデバイスも読まなくなり、PC の書き込みが待ちます
（捨てる代わりに押し戻す）。部分転送と、送り手が詰まる様子を再現します。

## 必要なものと配線

- USB デバイスのある石と、PC への USB の線。115200 bps の UART モニタを `RX` / `TX` に。
- PC に Python と PyUSB（`pip install pyusb`）と libusb。Linux は機器へのアクセス権、Windows は WinUSB の割り当て
  （[USBVendor](../USBVendor/README.ja.md) と同じ）。

## 操作と期待する結果

書き込んで USB をつなぎ、PC で `python3 vendor_bulk_peer.py` を実行します。

```
steady: 30104 bytes sent, 30104 back, same
stalled: the write timed out (...)
stalled: the write took 704 bytes before the device stopped reading, 704 came back
after resuming: 8192 of 8192 back, in order, none lost
```

1. **steady**: 1〜300 バイトのばらばらの塊を、読みながら送ります。順番どおりに全部戻ります。
2. **stalled**: 8 KB を読まずに送ります。デバイスの IN 側が満杯になると、デバイスは読むのをやめ、PC の書き込みは途中で
   時間切れになります（どこまで受けたかはデバイスの送受信の FIFO と端点の大きさで決まる）。そのあと PC が読み、残りを
   送ると、全部が順番どおりに戻ります。

UART には毎秒 `moved ... bytes, IN full ... times` が出ます。stalled の段で `IN full` が増えます。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `vendor.writeAvailable()` | IN 側の FIFO の空き。これ以下しか読まないので、`write()` は必ず全部受けます。 |
| `vendor.available()` / `read()` | OUT から届いた分。読まない分は端点に残り、ホストの書き込みが待ちます。 |
| `vendor.write()` / `flush()` | 送る。`write()` は入る分だけ受けて、その数を返します（全部を待たない）。 |

[USBVendor](../USBVendor/README.ja.md) のエコーは `write()` の戻り値を見ていないので、PC が読まないと送りきれない分を捨てます。
こちらは読む量を空きで抑えて、失わない形です。

CH32H417 の SuperSpeed で確かめました（2026-11-20）: 3 つの段とも全部のバイトが順に戻り、止まった書き込みは 3 KB（デバイスの
バッファ）で待ち、PC が読むと続きました。
