# PinInterrupt

[English](README.md)

GPIO のエッジを割り込みで数え、表示を `loop()` へ分けます。

## 必要なものと配線

`BUTTON` と GND の間にボタンをつなぎます。`INPUT_PULLUP` により、離した状態は High、押下時は Low です。UART モニタを用意します。

## 操作と期待する結果

ボタンを押すと `edges:` の値が増えます。接点の揺れがあるので、押下回数と完全一致するとは限りません。

## API と処理の流れ

`attachInterrupt(digitalPinToInterrupt(pin), handler, FALLING)` で下降エッジを登録します。ISR は `volatile` のカウンタを増やすだけにし、`loop()` で一度コピーして表示します。

## 改造するときの注意

ISR 内で表示や待ちを行いません。`volatile` は複数操作を不可分にする仕組みではありません。値を複数まとめて受け渡す場合は [CH32RVSyncKit](../../../CH32RVSyncKit/README.ja.md) を参照してください。同じ bit 番号の EXTI の競合にも注意します。

## 関連資料

- [スケッチ](PinInterrupt.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
