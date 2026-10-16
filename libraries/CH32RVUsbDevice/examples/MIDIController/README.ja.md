# MIDIController

English: [README.md](README.md)

可変抵抗で CC74、ボタンで Note 60 を送る USB MIDI コントローラです。

## 必要なものと配線

可変抵抗の両端をボードの ADC 基準電圧側と GND、摺動子を `A0` へ接続します。ボタンは `PA1`–GND。PC の MIDI モニタと UART モニタを用意します。

## 操作と期待する結果

1. 可変抵抗を回すと `MIDI_CC control=74 value=...` が出て、PC へ CC 値が送られます。既定の10-bit ADCでは、この例の変換式による出力は約0～31です。
2. PA1 ボタンを押すと channel 0 の Note On 60、離すと Note Off が送られます。
3. UART に `MIDI_BUTTON ON/OFF` が出ることを確認します。

## 設定と制約

コードの `map(..., 0, 4095, 0, 127)` は12-bit ADCを前提としますが、このコアの既定は10-bitです。0～127の全範囲を使うには、`setup()` の最初の `analogRead()` より前に `analogReadResolution(12);` を追加するか、変換式の上限を既定の `1023` に変更します。ADC は15/16の平滑化付きです。ボタンの専用 debounce はないので、実際のスイッチで複数イベントが出る場合は追加します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDIController.ino](MIDIController.ino)
