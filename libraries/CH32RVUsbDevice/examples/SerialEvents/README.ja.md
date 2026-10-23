# SerialEvents

English: [README.md](README.md)

USB の callback がどこで動くかを確かめる例です。`onRx()`・`onLineState()`・`onLineCoding()` は、`loop()` の
`deliverEvents()` の中でだけ動きます。10 秒ごとに `loop()` が `delay()` で 3 秒ふさがり、その間も USB の
通信は進みますが、callback は動きません。

## 必要なものと配線

USB Device を使える板と USB データケーブル、PC のシリアル端末（CDC）、115200 bps・8N1 の UART モニタ。
UART は `RX` / `TX` に対応する端子を使います。

## 操作と期待する結果

1. UART モニタを開いてから、USB を PC に接続します。`SerialEvents ready` が出ます。
2. 端末で CDC のポートを開くと、UART に `LINE_CODING baud=...`、`LINE_STATE dtr=1 rts=...`、
   `DELIVER ran=...` が出ます。
3. 端末で `abc` を送ると、同じ文字が端末に返り、UART に `RX available=3 echoed=3` と `DELIVER ran=1` が出ます
   （端末が 1 文字ずつ送る設定なら、1 文字ごとに出ます）。
4. UART に `BUSY 3 s: type now, the callbacks wait` が出ている間に何文字か打ちます。その間は何も返りません。
5. `BUSY end` のあと、打った文字がまとめて返り、`RX available=n echoed=n` が 1 行だけ出ます。
   届いたデータは捨てられず、ポートの buffer で待っていました。

## 設定と制約

- `deliverEvents()` を呼ばない限り callback は動きません。`delay()` の中では USB の転送は進みますが、
  callback は動きません。
- 届く前に何度か起きた同じ種類の通知は、1 回にまとまります。`onRx()` の引数はその時点で読める数です。
- 受信の buffer（Full Speed では 256 バイト）が満杯になると、PC 側が送るのを待ちます。データは失われません。
- `SerialUSB` が USB の CDC、`Serial` が UART です。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `onRx(onRx)` | データ到着を通知します。引数は通知時点の受信量であり、アプリのメッセージ長ではありません。 |
| `onLineState()` / `onLineCoding()` | DTR/RTS とホストの通信条件を、スケッチ側で受け取ります。 |
| `deliverEvents()` | 登録した通知 callback をその場で実行し、実行数を返します。USB packet 数ではありません。 |
| `delay()` | 待機中も USB の内部処理は進みますが、登録した通知 callback は呼びません。 |

`BUSY 3 s` の間に短い文字列を送り、`BUSY end` のあとにエコーと `RX` ログが出る順番を確認します。
複数の到着通知はまとめられるので、通知回数と送信回数の一致を前提にせず、`available()` を見て読み出します。
先に別の場所で受信データを読み切ると、`onRx()` は呼ばれない場合があります。

この仕組みは容量無制限の保存を意味しません。読み出しが遅いと受信バッファが埋まり、USB の相手側で待ちが発生します。
実際のコマンド処理は短く区切り、長い作業は状態を保存して次の `loop()` へ進めます。

## 関連資料

- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)（割り込み、待たない書き方、callback）
- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)

[SerialEvents.ino](SerialEvents.ino)
