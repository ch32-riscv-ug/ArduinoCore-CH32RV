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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `analogRead(CONTROLLER_PIN)` | 可変抵抗の値を読みます。読み取り範囲と `map()` の入力範囲を一致させます。 |
| `MIDI.controlChange(channel, control, value)` | 平滑化した値が変わったときだけ CC を送ります。channel は0始まりです。 |
| `noteOn()` / `noteOff()` | ボタンの押下で発音、解放で停止を送ります。`INPUT_PULLUP` なので押下は Low です。 |

可変抵抗の両端を基板の ADC 入力範囲内の電源と GND、中央を `CONTROLLER_PIN` へつなぎます。
ADC の分解能を変えたら、初期読み出しも含めて同じ設定にし、CC が端から端まで動くことをモニタで確認します。
平滑化は ADC の揺れを抑える処理であり、ボタンの debounce にはなりません。

この例は CC やボタン状態を送信前に更新するため、失敗した値を同じ状態のまま自動で再送しません。
実機のコントローラでは、送信成功時にだけ「送信済み状態」を更新するか、未送信の状態を次回へ持ち越します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDIController.ino](MIDIController.ino)
