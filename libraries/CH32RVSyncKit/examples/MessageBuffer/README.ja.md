# MessageBuffer

English: [README.md](README.md)

長さの違うメッセージを丸ごと扱います。`trySend()` は全部入れるか何も入れないか、`tryReceive()` は 1 件丸ごと取り出します。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 押すたびに短い（`hi`）・長い（`a longer message`）メッセージが交互に入り、2 秒ごとに `MESSAGE ...` で出ます。
2. 2 秒の間に何度も押すと 48 バイトがあふれ、入らなかった分が `REFUSED n` になります。

## 設定と制約

1 件は長さ + 2 バイトを使います。受け取り側の buffer が小さいと `tryReceive()` は 0 を返してメッセージを残すので、`nextLength()` で必要な大きさを確かめます。

## API の使い方と改造のポイント

`trySend(data, bytes)` はメッセージ全体を保存できれば成功し、容量不足なら何も追加しません。`tryReceive(dst, capacity)` は取得した長さを返します。文字列として表示するときは終端分を残します。

先頭メッセージが配列へ入らないと、後ろに短いものがあっても先へ進めません。`nextLength()` で必要量を調べ、保存する最大長と読み出し先の容量をそろえます。失敗回数を正確に集計する場合は、補助カウンタの取得とリセットも保護します。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[MessageBuffer.ino](MessageBuffer.ino)
