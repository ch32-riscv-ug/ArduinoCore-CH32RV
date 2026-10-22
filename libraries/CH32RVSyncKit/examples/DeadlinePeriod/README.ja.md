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

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[DeadlinePeriod.ino](DeadlinePeriod.ino)
