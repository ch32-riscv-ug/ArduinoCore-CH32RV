# WorkStats

English: [README.md](README.md)

どの仕事が遅いかを数字で見ます。仕事ごとに回数と時間を数え、実行器は巡回の回数を数えます。片方の仕事はわざと約 3 ms かかります。

## 必要なものと配線

115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 2 秒ごとに `FAST runs=... avg_us=... max_us=...`、`SLOW ...`、`ROUNDS total=... empty=... max_us=...` が出ます。
2. `SLOW` の `max_us` が 3000 前後、`ROUNDS` の `max_us` もそれ以上になり、遅い仕事が他を待たせていることが分かります。

## 設定と制約

時間には途中の割り込みの分も入ります（CPU 時間ではない）。最大値は見えた値で、保証ではありません。`empty` は何も起動しなかった巡回の数で、CPU が空いていた時間ではありません。数えない仕事・実行器には費用がかかりません。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVTaskKit の API](../../README.ja.md)

[WorkStats.ino](WorkStats.ino)
