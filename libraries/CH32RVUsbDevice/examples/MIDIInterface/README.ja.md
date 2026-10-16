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

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDIInterface.ino](MIDIInterface.ino)
