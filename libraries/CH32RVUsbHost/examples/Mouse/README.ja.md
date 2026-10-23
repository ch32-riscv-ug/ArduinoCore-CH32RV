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

## API の使い方と改造のポイント

| API・値 | この例での使い方 |
|---|---|
| `usb.onMouse(callback)` | マウスの report から解析したイベントを `deliverEvents()` で受け取ります。 |
| `e.x` / `e.y` / `e.wheel` | 相対量です。ポインタの絶対位置や画面ピクセル数を直接示しません。 |
| `e.buttons` | その時点のボタン状態の bitmap です。クリック回数ではありません。 |

ボタンの押下・解放を知りたい場合は、前回の bitmap との XOR で変化した bit を求めます。
イベントのデータは callback 中に使い、後で必要なら自分の状態変数へ保存します。

機器独自の追加操作が表示されない場合は、まず [DeviceInfo](../DeviceInfo/README.ja.md) と report descriptor で
該当 interface を確認します。高頻度の入力を全部 UART 表示すると処理が遅れるため、移動量を加算して定期表示する形も使えます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[Mouse.ino](Mouse.ino)
