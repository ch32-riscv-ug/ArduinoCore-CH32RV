# SPILoopback

ハードウェア SPI の送信と受信を、MOSI–MISO の折り返しで確認します。

## 必要なものと配線

`MOSI_PIN` と `MISO_PIN` をジャンパで接続します。UART モニタを用意し、スケッチのバス端子と UART 端子を基板へ合わせます。

## 操作と期待する結果

UART の sent と got が一致して `ok` になることを確認します。これは外部の SPI スレーブを制御する試験ではありません。

## API と処理の流れ

`SPI.begin(SCK, MISO, MOSI, -1)` でバスを開きます。`beginTransaction(SPISettings(clockHz, order, mode))` と `endTransaction()` の間で `transfer(byte)` を呼ぶと、送信と同時に受信した1 byteが返ります。

## 改造するときの注意

端子の順は SCK・MISO・MOSI です。実機器をつなぐ場合は仕様に合う mode・周波数と CS 制御を追加します。ループバックだけでは接続機器の CS やプロトコルは確認できません。

## 関連資料

- [スケッチ](SPILoopback.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
