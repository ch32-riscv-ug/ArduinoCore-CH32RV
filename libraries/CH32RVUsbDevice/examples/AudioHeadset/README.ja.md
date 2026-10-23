# AudioHeadset

English: [README.md](README.md)

UAC1 Playback と Capture を同時に公開し、PC から再生された PCM を録音側へ返すループバックです。

## 必要なものと配線

再生と録音を同時に行える PC アプリ、UART モニタ。物理的なヘッドセットは不要です。ビルド対象と容量条件は付属の `sketch.yaml` を参照してください。

## 操作と期待する結果

1. 再生・録音ともこの USB Headset を選びます。
2. 32000 Hz、16-bit、mono で両方向を開始します。
3. 再生したテスト音が録音されることを確認します。
4. UART の `AUDIO_STREAM target=... enabled=...` で各 stream の状態を確認します。

## 設定と制約

二つの stream は一つの clock を共有し、同じ sample rate を使います。音声は USB→RAM FIFO→USB の経路で、アナログ入出力はありません。例は capture の部分書き込みを処理しないため、stream開始時刻の差や過負荷での完全な無欠落転送を保証するものではありません。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `addPlaybackStream()` / `addCaptureStream()` | 受信用と送信用の二つの stream を追加します。 |
| 各 stream の `addFormat()` | 同じ PCM format を登録し、変換せずにループバックできるようにします。 |
| `playback.read()` / `capture.write()` | 取得したバイト数だけ返信します。どちらも実際に処理した長さを返します。 |
| `audio.pollEvent()` | `target` と `enabled` を確認し、両方向の開始・停止を区別します。 |

ホストは再生と録音を独立に開始できます。再生だけ開始している間は録音側の FIFO が埋まることもあるため、
製品に応用する場合は両 stream の状態を見て、古い PCM を送るか捨てるかを決めます。

音声を加工するときは、channel ごとのサンプル位置と符号付き PCM の範囲に注意します。
`capture.write()` の戻り値が `received` より短い場合、この例は残りを保持しません。欠落を避けるには
保留バッファを設け、未送信分を上書きしないようにします。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioHeadset.ino](AudioHeadset.ino)
