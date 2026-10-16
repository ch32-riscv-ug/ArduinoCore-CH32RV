# DfuRuntime

English: [README.md](README.md)

CDC を持たない DFU runtime インターフェースで、USB から ISP への移行を試します。PA1 が Low の間は `onDetach()` が移行を拒否します。

## 必要なものと配線

USB ISP 入口を実装済みの CH32X035/X033、PC、必要なら PA1–GND 間のボタン。UART ログはありません。

## 操作と期待する結果

1. スケッチを起動し、PC から DFU runtime インターフェースを確認します。
2. PA1 を開放した状態で `dfu-util -d 1209:0001 -e` を実行します。
3. X035/X033 ではアプリが切断され、WCH ISP の `1a86:55e0` が現れます。
4. スケッチへ戻った後、PA1 を GND に接続して同じ DETACH を試すと、アプリが残ることを確認できます。

## 設定と制約

これは DFU_DNLOAD による firmware 書き込みの例ではありません。移行後の書き込みは `ch32rv` の WCH ISP 機能を使います。Linux のアクセス権と Windows の WinUSB バインドは共通ガイドを参照してください。

既定で DFU runtime が付くため、通常は明示オブジェクトなしでも入口を使えます。この例で宣言するのは拒否 callback を登録するためです。ほかの系列では現在、ISP への移行ではなく通常 reset になります。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[DfuRuntime.ino](DfuRuntime.ino)
