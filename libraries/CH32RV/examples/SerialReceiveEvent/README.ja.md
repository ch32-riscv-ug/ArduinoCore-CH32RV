# SerialReceiveEvent

[English](README.md)

`available()` を見て回る代わりに、線が静かになったら届いたまとまりを一度に受け取り（ESP32 の `onReceive()`）、
受信の誤りも知らせてもらいます（`onReceiveError()`）。

## 必要なものと配線

`Serial` に USB-UART アダプタ。アダプタ RX←ボード TX、TX→RX、GND 共通。

## 操作と期待する結果

115200 8N1 で開き、行末なしで `hello` を送ります。ボードは `Got 5 bytes: hello` を 1 回出します（1 バイトごとではなく、
3 フレーム分静かになった後）。別の速さで送ると `Receive error mask 0x10`（ビット 4 の `UART_FRAME_ERROR`）などが出ます。

## API と処理の流れ

`setRxTimeout(3)` で 3 フレーム分の無信号をタイムアウトにし、`onReceive(onMessage, true)` はそのときだけ
`onMessage()` を呼びます（`false` なら、受信バッファが `setRxFIFOFull()` のバイト数（既定はバッファの半分）に
なったときも呼びます）。どちらのコールバックも割り込みではなく `loop()` の後（と `deliverEvents()`）で動くので、
読み出しや表示をしてかまいません。最初の 1 フレームは USART の IDLE のフラグ、残りは配信のときに数えるので、
`loop()` が長いと遅れることはあっても早まることはありません。

## 改造するとき

受信バッファ（既定 63 バイト）より長いまとまりは、タイムアウトの前にあふれます。`onlyOnTimeout` を false にして
コールバックで読むか、`CH32RV_SERIAL_RX_BUFFER_SIZE` を大きくします。誤りの種類は `hardwareSerial_error_t` に
あります。`UART_NOISE_ERROR` はこのコアのもので、そのバイトは残っています。

## 関連する文書

- [スケッチ](SerialReceiveEvent.ino)
- [例の準備と一覧](../README.ja.md)
