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

## 関連資料

- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)（割り込み、待たない書き方、callback）
- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)

[SerialEvents.ino](SerialEvents.ino)
