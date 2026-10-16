# DeviceInfo

English: [README.md](README.md)

接続機器の identity、文字列、interface、endpoint を UART に表示します。Host 配線の最初の確認に使います。

## 必要なものと配線

Host 対応の正確な型番、5 V VBUS を供給する Host 接続基板、任意の USB Device、115200 bps UART モニタ。

## 操作と期待する結果

1. USB Device を接続すると `connected:` と descriptor 情報が表示されます。
2. VID:PID、速度、interface class、endpoint の向き・packet size を確認します。
3. 抜くと `disconnected:` と機器情報が出ます。
4. 複合機器では interface ごとの class を確認して、次に使うサンプルを選びます。

## 設定と制約

この例は接続/切断時の表示だけで、Esp 側の同名例にある周期再表示や診断値のすべてを実装しているわけではありません。`Unsupported` はまずボードの型番を確認します。列挙できても、そのクラスの機能が対応済みとは限りません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[DeviceInfo.ino](DeviceInfo.ino)
