# CycleVoltages

English: [README.md](README.md)

ボタン操作で充電器の固定 PDO を順に選び、契約値を UART に表示します。

## 必要なものと配線

CH32X035/X033、PD 充電器、PA1–GND のボタン、UART モニタ。基板の VBUS 入力と負荷は、充電器が広告する最大固定電圧に対応する必要があります。

## 操作と期待する結果

1. 接続後、`contract: ... mV, up to ... mA` を確認します。
2. PA1 のボタンを押すと、現在位置の次の固定 PDO を要求します。末尾の次は先頭へ戻ります。
3. 受理された契約を UART で確認し、必要なら外部計測で VBUS と照合します。
4. 要求に失敗した場合は `the charger refused` と出ます。

## 設定と制約

PPS / battery / variable は循環の対象外です。ボタンは INPUT_PULLUP、押下の falling edge で処理し、専用 debounce はありません。接点の揺れで複数段進む場合は debounce を加えます。`maintain()` は loop ごとに呼びます。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbPd の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CycleVoltages.ino](CycleVoltages.ino)
