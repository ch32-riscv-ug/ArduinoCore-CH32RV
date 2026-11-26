# I2CScanner

[English](README.md)

I2C のアドレスへ応答する機器を探します。

## 必要なものと配線

SDA・SCL・GND を接続します。SDA と SCL には適切な電源への pull-up 抵抗が必要です。モジュールに既にある抵抗と電圧も確認し、UART モニタを用意します。

## 操作と期待する結果

5 秒ごとにスキャンし、使っている端子（`SDA=PC18 SCL=PC19` のように名前で）と、応答のあるアドレスを表示します。`bus timed out`（`endTransmission()` が 5）は、SCL か SDA が High に戻らない状態です。プルアップが無い、機器が線を Low に押さえている、端子が実際には I2C になっていない、のどれかです。`these pins cannot carry I2C here` は、その端子の組がこの石の I2C の経路に無いか、まだ手放していないデバッグの端子です。

## デバッグの端子を使うとき

X035 の PC18 / PC19 のように、デバッグの端子を I2C に使うときは、スケッチの `USE_DEBUG_PADS` を 1 にします（`CH32RV.releaseDebugPins()` を呼びます）。
次のリセットまでデバッガーが端子を手放し、その間は WCH-Link や OEP の probe から接続できません。書き込みは USB（WeAct の X035 なら
`ch32rv-app://` の口かシリアルポート）でします。出力先の `Console` は `SerialUSB` にします。USB のデバイスを動かさないスケッチだと、次の書き込みの手段が無くなり、基板の BOOT / DOWNLOAD キーでしか書き直せません（WeAct X035 で実際に起きました）。

## API と処理の流れ

`Wire.begin(SDA, SCL)` でバスを開きます。`beginTransmission(address)` のあと `endTransmission()` で結果を調べ、戻り値0なら ACK を得ています。アドレスは7 bit表記で渡します。

## 改造するときの注意

応答は機器の種類や正常動作の保証ではありません。見つけたアドレスへ、その機器に合ったレジスタ読み書きで確認します。データシートの read/write bit 付きアドレスをそのまま渡さないようにします。

## 関連資料

- [スケッチ](I2CScanner.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
