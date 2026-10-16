# AudioMicrophone

English: [README.md](README.md)

実マイクの代わりに440 Hzのテスト音を生成する UAC1 Capture です。

## 必要なものと配線

録音アプリを使える PC と UART モニタ。外付けマイクは不要です。flash 48 KiB以上の profile 条件があります。

## 操作と期待する結果

1. PC の入力デバイスにこの USB Microphone を選びます。
2. 32000 Hz、16-bit、mono で録音します。
3. 波形または周波数解析で約440 Hzの音を確認します。
4. UART の `AUDIO_CAPTURE enabled=1 alt=...` はホストが stream を開始した目印です。

## 設定と制約

64要素の table と32-bit phaseで音を作り、経過時間に応じた sample 数を送ります。長い停止後の追いつき処理は10 msに制限しています。`capture.write()` の戻り値を監視していない簡易例なので、実アプリでは FIFO の空きと部分書き込みを扱ってください。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[AudioMicrophone.ino](AudioMicrophone.ino)
