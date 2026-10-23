# DfuRuntime

English: [README.md](README.md)

CDC を持たない DFU runtime インターフェースで、USB から更新への移行を試します。PA1 が Low の間は `onDetach()` が移行を拒否します。

## 必要なものと配線

USB ISP 入口を実装済みの CH32X035/X033、PC、必要なら PA1–GND 間のボタン。UART ログはありません。

## 操作と期待する結果

1. スケッチを起動し、PC から DFU runtime インターフェースを確認します。
2. PA1 を開放した状態で `dfu-util -d 1209:0001 -e` を実行します。
3. X035/X033 ではアプリが切断され、WCH ISP の `1a86:55e0` が現れます。
4. スケッチへ戻った後、PA1 を GND に接続して同じ DETACH を試すと、アプリが残ることを確認できます。

## 設定と制約

上の操作は工場 ISP を使う場合の確認手順です。ほかの系列へ応用するときは移行先と書き込み方式を USB ガイドで確認します。
Linux のアクセス権と Windows の WinUSB バインドも確認します。

既定で DFU runtime が付くため、通常は明示オブジェクトなしでも入口を使えます。この例で宣言するのは拒否 callback を登録するためです。系列によっては自己書き換え用の DFU mode へ移行します。対応範囲は USB ガイドを参照してください。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceDfuRuntime dfu` | 定義済み `USBDevice` に、移行判定を設定できる DFU function を追加します。 |
| `dfu.onDetach(callback)` | 移行してよいとき `true`、止めたいとき `false` を返します。単なる事後通知ではありません。 |
| `USBDevice.begin(config)` | DFU interface を公開します。CDC が無いため COM ポートは作りません。 |

`onDetach()` は USB の要求処理中に呼ばれるので、短い状態確認だけにします。ボタンだけでなく、
書き込み中・動作中といったスケッチの状態を見て移行を見送る用途にも使えます。
拒否してもアプリの状態を変えないよう、判定と後処理を分けます。

DFU runtime は更新へ移るための interface です。移行先が工場 ISP か DFU mode か、実際にどのツールで
書き込むかは系列の実装によって異なります。[USB の書き込み手順](../../../../docs/usb.ja.md#書き込み) を確認し、
runtime が見えたことだけで `dfu-util -D` が使えるとは判断しません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[DfuRuntime.ino](DfuRuntime.ino)
