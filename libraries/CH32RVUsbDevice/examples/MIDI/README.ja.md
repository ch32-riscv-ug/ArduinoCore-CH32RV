# MIDI

English: [README.md](README.md)

USB MIDI 1.0 の Note / Control Change を自動送信し、受信 packet を UART に表示します。

## 必要なものと配線

PC の MIDI モニタ、音を確認するならソフトウェア音源、UART モニタ。

## 操作と期待する結果

1. PC の MIDI 入力にボードを選びます。
2. 約1秒ごとにノート60→64→67→72が移り、velocity 96 と CC74 の変化が見えます。
3. PC からボードへ MIDI を送ると UART に `MIDI_RX` が出ます。

## 設定と制約

API の channel 0 は通常の表記で MIDI channel 1、cable は既定0です。MIDI メッセージは音声 PCM ではなく、音を出すにはホスト側の音源が必要です。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)
