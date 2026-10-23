# MediaKeys

English: [README.md](README.md)

音量・再生・トラック操作の Consumer Control と、System Control の宣言例です。

## 必要なものと配線

音量表示やメディア再生を確認できる PC と UART モニタ。接続後、自動で音量やミュートが変わります。

## 操作と期待する結果

1. 約3秒ごとに volume up、volume down、mute、play/pause、next、previous が順に送られます。
2. UART の `MEDIA_KEY` とホストの反応を照合します。
3. プレーヤーが対象 usage に応答しない場合もあるため、必要なら HID report で確認します。

## 設定と制約

電源オフ・standby・wake の呼出しはコメントアウトされており、そのままでは送信しません。実機ボタン等の明示操作へ割り当てる際に有効化します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceHidConsumerControl media(device)` | 音量・再生操作の Consumer Control を追加します。 |
| `media.click(usage)` | 指定 usage の押下と解放を送ります。キーボードの文字コードは渡しません。 |
| `CH32RVUsbDeviceHidSystemControl systemControl(device)` | ホストの電源状態に関わる別の HID 機能を追加します。 |

送信成功は、ホストのプレーヤーがその操作を実行した保証ではありません。音量は OS、トラック操作は
アプリ側の対応も必要です。`bool` の結果で USB の失敗と、ホスト側の反応の有無を切り分けます。

ボタンへ割り当てる場合は debounce を入れ、押した瞬間だけ `click()` します。押している間の毎回の `loop()` で
呼ぶと繰り返し操作になります。System Control の有効化は、個々の usage の影響を確かめてから行います。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MediaKeys.ino](MediaKeys.ino)
