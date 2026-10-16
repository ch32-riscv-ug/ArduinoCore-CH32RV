# AudioHeadset

English: [README.md](README.md)

UAC1 Playback と Capture を同時に公開し、PC から再生された PCM を録音側へ返すループバックです。

## 必要なものと配線

再生と録音を同時に行える PC アプリ、UART モニタ。物理的なヘッドセットは不要です。flash 48 KiB以上の profile 条件があります。

## 操作と期待する結果

1. 再生・録音ともこの USB Headset を選びます。
2. 32000 Hz、16-bit、mono で両方向を開始します。
3. 再生したテスト音が録音されることを確認します。
4. UART の `AUDIO_STREAM target=... enabled=...` で各 stream の状態を確認します。

## 設定と制約

二つの stream は一つの clock を共有し、同じ sample rate を使います。音声は USB→RAM FIFO→USB の経路で、アナログ入出力はありません。例は capture の部分書き込みを処理しないため、stream開始時刻の差や過負荷での完全な無欠落転送を保証するものではありません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioHeadset.ino](AudioHeadset.ino)
