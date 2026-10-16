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

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MediaKeys.ino](MediaKeys.ino)
