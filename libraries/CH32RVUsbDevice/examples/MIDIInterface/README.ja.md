# MIDIInterface

English: [README.md](README.md)

USB MIDI と 31250 bps UART の MIDI メッセージを双方向に変換する例です。

## 必要なものと配線

`Serial1` の RX=`PA3`、TX=`PA2` に適した MIDI IN/OUT 回路を接続します。DIN ケーブルは GPIO へ直結せず、受信絶縁・送信回路を介します。UART ログ用の `Serial` と同じ USART/端子にならないようルートを確認します。

## 操作と期待する結果

1. PC の MIDI モニタと UART 側の MIDI 機器を接続します。
2. UART 側で channel message を送ると、PC と UART ログの `DIN_TO_USB` で確認できます。
3. PC から送ると UART の `USB_TO_DIN` と対向 MIDI 機器で確認できます。

## 設定と制約

UART→USB の parser は status byte で始まる channel voice message のみです。running status、SysEx、system realtime の完全な parser ではありません。残りの bytes を最大10 ms待ち、1回の loop で UART メッセージを一つ処理します。対応範囲を増やす場合は parser を置き換えます。

## API の使い方と改造のポイント

| API・処理 | この例での使い方 |
|---|---|
| `Serial1.begin(31250, SERIAL_8N1, ...)` | MIDI 用 UART の配線と通信条件を指定します。UART ログの `Serial` と別にします。 |
| `MIDI.writePacket(packet)` | UART の status/data bytes を、CIN を付けた USB MIDI packet として送ります。 |
| `MIDI.readPacket(packet)` | USB 側から packet を取得します。CIN に応じた数だけ MIDI bytes を UART へ書きます。 |
| `Serial1.flush()` | UART の送信完了を待ちます。受信データの破棄ではありません。 |

DIN MIDI の電気回路と MCU の UART は直結できません。MIDI IN/OUT に適した回路を挟み、
USB 側のケーブルと UART の RX/TX、ログ用 UART を混同しないようにします。

UART→USB の受信処理は基本的な channel voice メッセージを読む簡易パーサーです。running status、SysEx、
途中へ挿入される real-time メッセージなどを扱う汎用 MIDI パーサーではありません。対応を広げるなら、
受信途中の状態を保持するパーサーへ変更し、送信 API の失敗と受信タイムアウトも処理します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDIInterface.ino](MIDIInterface.ino)
