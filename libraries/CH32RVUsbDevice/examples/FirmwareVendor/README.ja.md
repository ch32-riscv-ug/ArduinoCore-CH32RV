# FirmwareVendor

[English](README.md)

ベンダーのインタフェースでファームウェアを入れ替えます（EspUsbDevice の FirmwareVendor と同じ手順）。命令は制御要求、
イメージはバルク OUT で運び、Update ライブラリが確かめてから入れます。PC の側は [firmware_vendor.py](firmware_vendor.py)
です。USB の入れ替えの道の中でいちばん速い（EP0 でなくバルク）代わりに、ホストの道具を自分で持つことになります。
同じことを標準の道具でするのは [FirmwareDFU](../FirmwareDFU/README.ja.md) です。

## 必要なものと配線

USB のデバイスの口のある品と、PC への USB のケーブル。進み具合は 115200 bps の UART（`RX` / `TX`）に出ます。PC には Python と
pyusb（Linux ではデバイスのファイルに書ける権限: 1209:0001 の udev のルールか sudo）。

## 手順（プロトコル）

| 向き | 要求 | 意味 |
|---|---|---|
| 制御 OUT `0x40` | `0x01`、`wValue` に大きさの 0〜15 ビット、`wIndex` に 16〜31 ビット | 開始（`Update.begin(size)`） |
| バルク OUT | - | イメージ |
| 制御 OUT `0x40` | `0x02` | 確定。`Update.end()` が確かめ、デバイスは新しいスケッチで起動し直す。確かめで落ちれば stall |
| 制御 OUT `0x40` | `0x03` | 中止 |
| 制御 IN `0xC0` | `0x04` → 8 バイト | `[0]` 受け取り中、`[1..3]` Update ライブラリの誤り、`[4..7]` 書いたバイト数（リトルエンディアン） |

命令は EP0、データはバルクに分けるので、状態の要求はバルク OUT が動いている間もいつでも答えられ、ホストは進み具合を見られます。
ほかのベンダーの要求は stall します。

## 操作と期待する結果

1. 書き込むと、UART に `Ready: up to <bytes> bytes ...` が出ます。受け取れるイメージの最大です。
2. 新しいスケッチの `.bin`（Arduino の「コンパイルしたバイナリを出力」）を送ります:
   `uv run --with pyusb python3 firmware_vendor.py Blink.ino.bin`
3. `accepted` のあと、基板が新しいスケッチで動きます。
4. 大きすぎると、何も送る前に `device refused the image`。この石のスケッチでなければ、確定のときに
   `device refused to install it` で、このスケッチのままです。

flash のページを消している間、残りのイメージはデバイスの 2 KB の FIFO で待ちます（ホストは NAK で待たされる）ので、
失われません。入れ替え（先頭への写し）の途中で電源が切れると、スケッチは半分書かれたままです（プローブで戻せます）。
