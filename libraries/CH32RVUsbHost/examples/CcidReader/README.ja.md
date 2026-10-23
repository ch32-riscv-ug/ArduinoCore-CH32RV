# CcidReader

English: [README.md](README.md)

USB CCID リーダーでカードを起動し、ATR と SELECT MF の status word を表示します。

## 必要なものと配線

CCID 対応 USB リーダーと対応カード、Host の VBUS、UART モニタ。Device 側 SmartCardReader を peer にすることもできます。

## 操作と期待する結果

1. リーダー接続またはカード挿入で処理を開始します。
2. power on が成功すると `ATR: ...` を表示します。
3. `00 A4 00 00 02 3F 00`（SELECT MF）を送信し、成功した通信では `SELECT MF: SW=...` を表示します。
4. 抜去時は `card removed` が出ます。

## 設定と制約

APDU 交換成功と命令成功は別で、`9000` 以外の SW も返ります。Device 側 SmartCardReader の模擬カードは SELECT MF を実装していないため `6D00` が想定値です。`ccidApdu()` は response 本体と SW を分離して返しますが、response capacity は SW の2 bytesも収まる大きさが必要です。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `ccidReady()` / `ccidOpen()` | リーダーを使える状態か調べ、必要なら interface を開きます。カードの存在とは別です。 |
| `ccidPowerOn(atr, capacity, &length)` | カードを有効化し、ATR と実際の長さを受け取ります。失敗が必ずカード不在を意味するわけではありません。 |
| `ccidApdu(..., response, capacity, &length, &sw)` | APDU を交換します。response のデータと status word を分けて受け取ります。 |
| `onCcidCardInserted()` / `onCcidCardRemoved()` | 挿入・抜去の通知を受け、次に行う処理を決めます。 |

`ccidApdu()` が `true` でも、カードが命令を受理したとは限りません。USB/CCID の交換の成否とは別に、
`sw` を確認します。response 配列の容量は SW1/SW2 の領域も含めて確保し、返されたデータ長だけを使います。

SELECT MF に応じるかはカードによります。Device の [SmartCardReader](../../../CH32RVUsbDevice/examples/SmartCardReader/README.ja.md)
と組み合わせると、その模擬カードはこの命令へ未対応の SW を返すため、正常終了と同じ結果にはなりません。
「no card」だけでは分からない失敗を調べる場合は `lastErrorName()` と `ccidLastError()` を表示します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CcidReader.ino](CcidReader.ino)
