# SoftSPILoopback

GPIO でクロックを生成する SPI のループバックを確認します。

## 必要なものと配線

`MOSI_PIN` と `MISO_PIN` を接続し、UART モニタを用意します。三つの GPIO はほかの用途と重ならない端子を選びます。

## 操作と期待する結果

各 SPI mode のテスト結果を確認します。MISO 未接続時の値は入力が浮くため、通信成功の判定には使いません。

## API と処理の流れ

`SoftSPI bus(SCK, MISO, MOSI)` を宣言し、`bus.begin()` 後に transaction と `transfer()` を使います。ハードウェア SPI と同様に、戻り値は送信と同時に受けた値です。

## 改造するときの注意

ソフトウェア SPI は CPU で波形を作るため、指定した速度と実際の波形は同じとは限りません。割り込みやほかの仕事の影響がある用途は、測定器で確認します。実スレーブ用には CS 制御も追加します。

## 関連資料

- [スケッチ](SoftSPILoopback.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
