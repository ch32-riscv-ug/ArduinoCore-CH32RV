# CH32RVAdcStream サンプルの使い方

[English](README.md)

ADC を止めずに回し続け、DMA が埋めたバッファを受け取る例です。

## ボードと接続

ADC1 の DMA が DMA1 のチャネル 1 の系列で動きます（[ライブラリの README](../README.ja.md)）。ログは `Serial` のハードウェア UART へ 115200 bps・8N1 で出します。端子は各 `.ino` の定数で変えます。

## 一覧

| 例 | 学ぶこと |
|---|---|
| [AdcStreamStats](AdcStreamStats/README.ja.md) | 1 本の端子を連続して取り込み、埋まった半分を受け取って集計します。 |
