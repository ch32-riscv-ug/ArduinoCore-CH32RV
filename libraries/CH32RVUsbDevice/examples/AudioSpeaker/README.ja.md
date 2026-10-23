# AudioSpeaker

English: [README.md](README.md)

UAC1 Playback で受信した PCM を数えて UART に表示します。スピーカーや DAC への出力は実装していません。

## 必要なものと配線

USB audio を再生できる PC と UART モニタ。外付け音声回路は不要です。ビルド対象と容量条件は付属の `sketch.yaml` を参照してください。

## 操作と期待する結果

1. PC の出力デバイスにこの USB Speaker を選びます。
2. 16000 Hz、16-bit、stereo の PCM を再生します。
3. UART の `AUDIO_PLAYBACK enabled=1` と `AUDIO_RX` の増加を確認します。連続再生なら PCM は約64000 bytes/sです。
4. ホストから mute/volume を変えると対応 event が出ます。

## 設定と制約

format の順は `{sampleRate, channels, bytesPerSample, bitsPerSample}`。`read()` の PCM を別途 DAC/I2S 等へ渡すのが実製品の処理になります。音量は1/256 dBの制御値で、ライブラリが PCM に自動適用するものではありません。X035 の64 B/msに収めた設定なので、48 kHz stereoへ変更する場合はコントローラと endpoint の能力も確認します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `audio.addPlaybackStream()` | Host→Device の音声を受ける stream を追加します。戻り値の参照を保持します。 |
| `playback.addFormat({...})` | USB 起動前に PCM format を登録します。配列の各値の意味は上の「設定と制約」を参照します。 |
| `playback.read(pcm, sizeof(pcm))` | 取得したバイト数を返します。配列全体が必ず埋まるわけではありません。 |
| `audio.pollEvent(event)` | stream 状態・mute・volume のイベントを取り出します。この例は通知 callback の代わりにポーリングします。 |
| `playback.stats()` | 転送量や overrun を調べ、読み出しが間に合っているか確認します。 |

PCM の1フレームは「全 channel の1サンプル分」です。バイト数は `sampleRate × channels × bytesPerSample` で
1秒分を求められます。DAC 等へ渡す場合も、バイト数とサンプル数を取り違えず、実際に取得した分だけ処理します。

USB の列挙完了だけでは音声は届きません。ホストが再生 stream を開始していることをイベントで確認します。
音が途切れる場合は、長い UART 表示や重い処理を減らし、FIFO を定期的に読み出せる構成にします。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioSpeaker.ino](AudioSpeaker.ino)
