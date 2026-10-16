# AudioSpeaker

English: [README.md](README.md)

UAC1 Playback で受信した PCM を数えて UART に表示します。スピーカーや DAC への出力は実装していません。

## 必要なものと配線

USB audio を再生できる PC と UART モニタ。外付け音声回路は不要です。flash 48 KiB以上の profile 条件があります。

## 操作と期待する結果

1. PC の出力デバイスにこの USB Speaker を選びます。
2. 16000 Hz、16-bit、stereo の PCM を再生します。
3. UART の `AUDIO_PLAYBACK enabled=1` と `AUDIO_RX` の増加を確認します。連続再生なら PCM は約64000 bytes/sです。
4. ホストから mute/volume を変えると対応 event が出ます。

## 設定と制約

format の順は `{sampleRate, channels, bytesPerSample, bitsPerSample}`。`read()` の PCM を別途 DAC/I2S 等へ渡すのが実製品の処理になります。音量は1/256 dBの制御値で、ライブラリが PCM に自動適用するものではありません。X035 の64 B/msに収めた設定なので、48 kHz stereoへ変更する場合はコントローラと endpoint の能力も確認します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioSpeaker.ino](AudioSpeaker.ino)
