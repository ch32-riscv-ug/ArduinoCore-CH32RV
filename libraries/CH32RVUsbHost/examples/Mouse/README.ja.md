# Mouse

English: [README.md](README.md)

USB マウスの相対移動、ホイール、ボタン bitmap を UART で観察します。

## 必要なものと配線

USB マウス、Host の VBUS 配線、115200 bps UART モニタ。

## 操作と期待する結果

1. マウスを動かすと `x=... y=... wheel=... buttons=0x...` が出ます。
2. ホイールと各ボタンを操作し、値の変化を確認します。
3. 静止している間に出力がないのは正常です。

## 設定と制約

report descriptor から配置を解析しますが、すべての独自マウスの機能を保証するものではありません。ボードは PC へマウス入力を転送せず、UART へ値を表示するだけです。高速な連続ログで追いつかない場合は表示頻度を下げます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Mouse.ino](Mouse.ino)
