# AudioStreams

English: [README.md](README.md)

USB オーディオの機器（UAC1 / UAC2: USB のヘッドセット、マイク、スピーカー、オーディオのデバイスにした ESP32 など）の
PCM のストリームを表示し、出力に 440 Hz の正弦波を流して、入力のピークのレベルを 1 秒ごとに UART に表示します。

## 必要なものと配線

ホストの口のあるボード、5 V の VBUS を出すホストの配線、USB オーディオの機器、115200 bps の UART モニタ。
フルスピードの口では、48 kHz・16 ビット・ステレオで 1 ms に 192 バイトの等時の転送になります。

## 操作と期待する結果

機器をつなぐと、ストリームの一覧（向き、チャンネル数、ビット数、レート、UAC の版）が出て、出力から 440 Hz が鳴ります。
マイクに声を入れると `input peak` の値が大きくなります。`output underruns` は、出力のキューが空になった回数です
（loop() が長く止まると増えます）。

## 設定と制約

- 入力と出力はそれぞれ 1 つ。16 ビットのストリームを選び、48 kHz が無ければ機器の最初のレートで始めます。
- フィードバックの端点は使いません（非同期の機器にも名目のレートで送る）。音量とミュート（Feature Unit）はまだありません。
- PCM のリングは `CH32RV_USB_HOST_AUDIO_RING_SIZE`（既定 2048 バイト）で、開始のときにヒープから取ります。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `usb.getAudioStreams()` | 機器のストリームの形式とレートを読みます。 |
| `usb.audioInputStart()` / `onAudioData()` | 入力を始め、受けた PCM を `deliverEvents()` の中で受け取ります。 |
| `usb.audioOutputStart()` / `audioSend()` / `audioOutputAvailableForWrite()` | 出力を始め、空きがある分だけ PCM をためます。 |
| `usb.audioOutputUnderruns()` | 足りずに無音を送った回数です。 |
