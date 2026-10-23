# BasicWork

English: [README.md](README.md)

起動条件があるときだけ動く 2 つの仕事です。`Period` で LED を切り替え、UART にバイトが来たときだけ行を読みます。`loop()` は `runOnce()` を 1 回呼ぶだけで、どちらもそこでだけ動きます。

## 必要なものと配線

`LED_PIN`（既定 `PA1`）に抵抗付きの LED。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 起動すると `BasicWork ready`、LED が 250 ms ごとに切り替わります。
2. 行を送ると `LINE` が返ります。そのあいだも LED は止まりません。

## 設定と制約

`attach()` した仕事は最初の `runOnce()` で 1 回動き、以後は起動条件（`wakeEvery`、`wakeWhen`、`wakeOn`、`post()`）があるときだけ動きます。仕事は途中で待てないので、状態を変数に残して戻ります。

## API の使い方と改造のポイント

`Work` は普通の短い関数に起動条件を付けるオブジェクトで、`app.attach(work)` で実行器へ登録します。`wakeEvery(period)` は周期、`wakeWhen(predicate)` は条件を指定します。いずれも実行は `app.runOnce()` の中です。

受信条件が真でも行全体があるとは限らないため、関数内で改行を待ち続けず、途中の行を保存して戻ります。Work や起動条件に使うオブジェクトは実行器が使う間、有効に保ちます。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVTaskKit の API](../../README.ja.md)

[BasicWork.ino](BasicWork.ino)
