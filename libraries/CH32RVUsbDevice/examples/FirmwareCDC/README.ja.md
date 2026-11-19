# FirmwareCDC

[English](README.md)

USB のシリアルのポート（インターフェース名 `Firmware`）で新しいスケッチを受け取り、確かめてから入れ替えます（EspUsbDevice の
FirmwareCDC と同じ手順）。PC の側は [firmware_cdc.py](firmware_cdc.py) です。

```
ホスト "FW <大きさ>\n" -> デバイス "READY\n" -> その数のバイト -> デバイス "OK\n" -> 入れ替えて起動
```

## 必要なものと配線

USB デバイスのある石と、PC への USB の線。進み具合は 115200 bps の UART モニタ（`RX` / `TX`）。PC に Python と pyserial。

## 操作と期待する結果

1. 書き込むと UART に `Ready: up to <バイト数> bytes ...` と出ます。受け取れる最大です（flash − このスケッチ。V203 の
   32 KB では約 13 KB）。
2. 入れ替えるスケッチの `.bin`（Arduino の「コンパイルしたバイナリを出力」）を送ります:
   `python3 firmware_cdc.py /dev/ttyACM0 Blink.ino.bin`
3. `accepted ... bytes` のあと、ボードは新しいスケッチで起動します。
4. 大きすぎると、送る前に `device refused: ERR Not Enough Space`。スケッチでないものは `ERR Wrong Magic Byte` で、このスケッチの
   ままです。

## API と処理の流れ

| API | この例での使い方 |
|---|---|
| `Update.begin(size)` | 大きさを先に宣言。入らなければ、ホストが送る前に断る。 |
| `Update.write(buffer, n)` | 届いた順に書く。flash の消去は `loop()` の中で行い、その間はホストが待つ。 |
| `Update.end()` | イメージを確かめ、再起動で入れ替わる状態にする。 |
| `CH32RV.restart()` | 入れ替えて起動。 |

ほかの入れ方: DFU は [FirmwareDFU](../FirmwareDFU/README.ja.md)、ドライブへのコピーは [FirmwareMSC](../FirmwareMSC/README.ja.md)、
UART は Update ライブラリの SerialUpdate。入れ替え（先頭へのコピー）の間に電源が切れると、スケッチは書きかけになります
（プローブで直せる）。
