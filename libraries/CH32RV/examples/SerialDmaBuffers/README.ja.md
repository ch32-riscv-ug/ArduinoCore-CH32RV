# SerialDmaBuffers

[English](README.md)

`loop()` が 4 ms ずつほかの仕事をしている間も、1 Mbaud の流れを取りこぼさずに受け取ります。`setRxBufferSize()` と
`setTxBufferSize()`（ESP32 の名前）で `Serial` のバッファを大きくし、そのバイトは 1 バイトごとの割り込みではなく
DMA が運びます。

## 必要なものと配線

1 Mbaud で動く USB-UART アダプタ（CP2102N、FT232R、CH343 など）を `Serial` に。アダプタ RX←ボード TX、TX→RX、
GND 共通。

## 操作と期待する結果

アダプタを 1000000 8N1 で開き、ファイル（数百 KB）を送ります。ボードは 1 秒ごとに、そこまでに受け取ったバイト数と
その和を出します。数はファイルの大きさで止まり、`receive error mask` の行は出ません。`setRxBufferSize(512)` を
`setRxBufferSize(32)` にすると、4 ms 離れる間にバッファがあふれ、`receive error mask 0x4`
（`UART_BUFFER_FULL_ERROR`）でバイトが失われたことが分かります。

## API と処理の流れ

`setRxBufferSize()` / `setTxBufferSize()` は `begin()` の前に呼びます（ESP32 と同じ。開いているポートでは 0 を
返します）。組み込みの 64 バイトまでの大きさはその領域を、それより大きいものはヒープを使います。受信の DMA は
`begin()` から `end()` まで輪のように回り続けるので、割り込みが止まっていてもバイトは届きます。`available()` と
`read()` は見たときに DMA の位置を読みます。スケッチがバッファの持つ時間より長く離れると、新しいバイトが古いものに
上書きされ、次に見たときに `UART_BUFFER_FULL_ERROR` が知らされます。送信は `print()` がバッファを埋め、DMA が
まとめて送ります。

## 改造するとき

バッファは、スケッチが `Serial` から離れる一番長い時間に合わせます（1 秒に baud / 10 バイト）。CH32V003 の RAM は
全部で 2 KB です。USART ごとに DMA のチャネルは決まっています（CH32H41x はコアが選んだもの）。CH32RVAdcStream や
CH32RVLogicCapture が同じチャネルを使っているときは、その向きは組み込みのバッファと割り込みに戻ります。RTS の
フロー制御のときも同じです。

## 関連する文書

- [スケッチ](SerialDmaBuffers.ino)
- [例の準備と一覧](../README.ja.md)
