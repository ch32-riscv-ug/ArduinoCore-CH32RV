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

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `noteOn(channel, note, velocity)` / `noteOff(...)` | channel は0始まり、note と velocity は MIDI の7 bit値です。ノート開始だけでなく終了も送ります。 |
| `controlChange(channel, control, value)` | コントローラ番号と値を送ります。音源の対応によって実際の効果は変わります。 |
| `readPacket(packet)` | USB MIDI のイベント packet を一つ取り出し、取得できれば `true` を返します。 |
| `packet.header` | 上位側は cable、下位側は CIN で、MIDI status byte とは別です。 |

画面上の「channel 1」は API では `0` です。MIDI モニタで Note On / Note Off / CC を確認してから音源へ接続すると、
USB 通信の問題と音源の設定を切り分けられます。PC からの MIDI も送って `MIDI_RX` を確認します。

この例の周期ログは送信の試行を表示しており、すべての送信結果を確認していません。演奏用途へ変更するときは
送信 API の `bool` を確認し、特に Note Off の失敗を放置しないようにします。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)
