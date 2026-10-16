# SmartCardReader

English: [README.md](README.md)

カード自体をスケッチで模擬する1スロットの CCID リーダーです。実カードや接触型カード回路は接続しません。

## 必要なものと配線

PC/SC を使える PC と APDU 送信ツール、115200 bps の UART モニタ。

## 操作と期待する結果

1. PC のリーダー一覧と ATR を確認します。起動時はカード挿入状態です。
2. `FF CA 00 00 00` を送ると `04 11 22 33 90 00` が返ります。
3. `80 01 00 00 03 61 62 63` を送ると `61 62 63 90 00` が返ります。
4. UART に `r` を送るとカード抜去、`i` で再挿入します。約2秒ごとに状態と処理数を表示します。

## 設定と制約

それ以外の命令は `6D00` です。ATR はカード種別を模したものですが、そのカードの全機能を実装するものではありません。Device の APDU callback は payload と SW1/SW2 を連結して返します。Host の `ccidApdu()` が SW を別出力にする点と区別してください。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[SmartCardReader.ino](SmartCardReader.ino)
