# I2CScanner

I2C のアドレスへ応答する機器を探します。

## 必要なものと配線

SDA・SCL・GND を接続します。SDA と SCL には適切な電源への pull-up 抵抗が必要です。モジュールに既にある抵抗と電圧も確認し、UART モニタを用意します。

## 操作と期待する結果

起動時に一度スキャンし、応答のあるアドレスを表示します。`bus timed out` の場合は配線と pull-up を先に確認します。再実行はリセットします。

## API と処理の流れ

`Wire.begin(SDA, SCL)` でバスを開きます。`beginTransmission(address)` のあと `endTransmission()` で結果を調べ、戻り値0なら ACK を得ています。アドレスは7 bit表記で渡します。

## 改造するときの注意

応答は機器の種類や正常動作の保証ではありません。見つけたアドレスへ、その機器に合ったレジスタ読み書きで確認します。データシートの read/write bit 付きアドレスをそのまま渡さないようにします。

## 関連資料

- [スケッチ](I2CScanner.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
