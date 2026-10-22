# MIDI

English: [README.md](README.md)

USB MIDI 機器の受信メッセージを UART に表示し、Note 60 を周期的に送ります。

## 必要なものと配線

USB MIDI Device として動く鍵盤/音源、Host の VBUS、UART モニタ。USB Host 同士になる機器の端子へは接続しません。

## 操作と期待する結果

1. 機器を操作すると `cable ... status data1 data2` が UART に出ます。
2. MIDI Ready になっていれば、約1秒ごとに channel 0 へ Note 60・velocity 100 を送ります。
3. 約200 ms後に Note Off を送ります。音源機能がある機器なら発音を確認できます。

## 設定と制約

channel 0 は通常表記で channel 1です。音源のない MIDI キーボードは受信しても鳴りません。`delay(200)` の間も Host stack は進みます。callback は `loop()` の先頭の `deliverEvents()` で届きます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbHost の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[MIDI.ino](MIDI.ino)
