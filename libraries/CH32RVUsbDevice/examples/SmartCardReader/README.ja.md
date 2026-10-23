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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `Ccid.insertCard(atr, length)` / `removeCard()` | 仮想スロットの挿入・抜去状態をホストへ示します。物理カードの検出ではありません。 |
| `onApdu(apdu, length, response, capacity)` | コマンドを検査し、応答先へ SW1/SW2 を含めて書き、総バイト数を返す provider を登録します。 |
| `onPower()` | ホストによるカードの有効化・無効化を `deliverEvents()` で通知します。 |
| `cardPresent()` / `cardPowered()` | カードの存在とホストが有効化した状態を区別します。 |

APDU を追加するときは、header の長さと Lc に対して必要なデータがあるかを確認し、応答が `capacity` を
超えないようにします。callback の戻り値0は、正常な「長さ0のカード応答」ではなく処理失敗を表します。

`onApdu()` は USB の応答をその場で用意する処理なので、`deliverEvents()` を待たずに呼ばれます。
通知 callback と同じ感覚で待ち時間の長い処理を入れないでください。実カードリーダーを作るには別途カードの
電気回路と通信処理が必要で、この例の ATR だけでそのカードの動作が実現するわけではありません。

### 模擬ATRの意味

`CARD_ATR`はPC/SCの非接触storage card用ATRを模した値です。historical bytesにはRID `A0 00 00 03 06`、standard `03`、card name `00 01`（MIFARE Classic 1K）を含みます。例の識別用データであり、そのカードのメモリ・認証・無線プロトコルを実装するものではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[SmartCardReader.ino](SmartCardReader.ino)
