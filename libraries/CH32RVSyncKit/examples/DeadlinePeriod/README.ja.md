# DeadlinePeriod

English: [README.md](README.md)

`delay()` を使わずに 2 つの予定を同時に進めます。`Period` で LED を点滅し、`Deadline` で UART の入力を 5 秒待ちます。

## 必要なものと配線

`LED_PIN`（既定 `PA1`）に抵抗付きの LED。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. LED が 250 ms ごとに切り替わります。
2. `TYPE something within 5 s` のあと、5 秒以内に何か送ると `THANKS spare_ms=...`、送らなければ `TOO_SLOW`。どちらの間も LED は止まりません。

## 設定と制約

`Period` は `loop()` が何周期も遅れても 1 回だけ真になり、飛ばした回数は `missed()` で分かります（まとめて実行しない）。どちらも `loop()` の速さ以上には正確になりません。

## API の使い方と改造のポイント

`blink.due()` は周期が来たかを調べ、`answer.arm(ms)` は期限を設定します。`expired()` は期限を過ぎたか、`remaining()` は残りの ms を返します。これらは処理を自動で割り込ませるタイマーではなく、呼び出した時点で調べる API です。

例は最初の受信があれば入力成功とし、行が完成したかは検査しません。行全体を期限までに受けたい場合は、改行までの蓄積と期限管理を組み合わせます。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[DeadlinePeriod.ino](DeadlinePeriod.ino)
