# AudioMicrophone

English: [README.md](README.md)

実マイクの代わりに440 Hzのテスト音を生成する UAC1 Capture です。

## 必要なものと配線

録音アプリを使える PC と UART モニタ。外付けマイクは不要です。ビルド対象と容量条件は付属の `sketch.yaml` を参照してください。

## 操作と期待する結果

1. PC の入力デバイスにこの USB Microphone を選びます。
2. 32000 Hz、16-bit、mono で録音します。
3. 波形または周波数解析で約440 Hzの音を確認します。
4. UART の `AUDIO_CAPTURE enabled=1 alt=...` はホストが stream を開始した目印です。

## 設定と制約

64要素の table と32-bit phaseで音を作り、経過時間に応じた sample 数を送ります。長い停止後の追いつき処理は10 msに制限しています。`capture.write()` の戻り値を監視していない簡易例なので、実アプリでは FIFO の空きと部分書き込みを扱ってください。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `audio.addCaptureStream()` | Device→Host の録音 stream を追加します。 |
| `capture.addFormat({...})` | PC に知らせる PCM format を USB 起動前に設定します。 |
| `capture.write(samples, byteCount)` | PCM を送信 FIFO へ入れ、受け付けたバイト数を返します。第2引数はサンプル数ではありません。 |
| `capture.availableForWrite()` | 現在の空きバイト数を調べます。部分書き込みを扱うときに使います。 |

`micros()` の差から必要なサンプル数を求めるのは、`loop()` の実行速度に音程を依存させないためです。
テーブルの位置は位相で進めます。サンプルレートを変える場合は、USB の format と位相の増分もそろえます。

実 ADC のデータへ置き換える場合は、データの取得時刻、符号、バイト順、channel 順を PCM format に合わせます。
`write()` が要求長より短ければ未送信分を保持し、次の回で送るか破棄を数えるかを決めます。
USB が内部で動き続けても、スケッチが PCM を供給しなければ録音の連続性は保てません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioMicrophone.ino](AudioMicrophone.ino)
