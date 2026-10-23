# CycleVoltages

English: [README.md](README.md)

ボタン操作で充電器の固定 PDO を順に選び、契約値を UART に表示します。

## 必要なものと配線

CH32X035 などの USBPD のある系列(CC の端子はライブラリの README)、PD 充電器、PA1–GND のボタン(CH32V205 では PA1 が CC2 なので、`BUTTON_PIN` をほかの端子に変える)、UART モニタ。基板の VBUS 入力と負荷は、充電器が広告する最大固定電圧に対応する必要があります。

## 操作と期待する結果

1. 接続後、`contract: ... mV, up to ... mA` を確認します。
2. PA1 のボタンを押すと、現在位置の次の固定 PDO を要求します。末尾の次は先頭へ戻ります。
3. 受理された契約を UART で確認し、必要なら外部計測で VBUS と照合します。
4. 要求に失敗した場合は `the charger refused` と出ます。

## 設定と制約

PPS / battery / variable は循環の対象外です。ボタンは INPUT_PULLUP、押下の falling edge で処理し、専用 debounce はありません。接点の揺れで複数段進む場合は debounce を加えます。接続の維持はドライバが `delay()` と `loop()` の戻りで行います。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `profileCount()` / `profile(index).kind` | 広告一覧から固定 PDO を探します。index は0始まりです。 |
| `contractProfile()` | 現在選ばれている PDO の位置を得ます。契約が無い場合を扱うため、先に `ready()` を確認します。 |
| `requestProfile(index)` | 選んだ固定 PDO を要求します。PPS を選ぶなら電圧の指定も必要です。 |
| `voltage()` / `current()` | 要求成功後の契約値を表示します。外部電圧計の代わりにはなりません。 |

ボタンは前回 High・今回 Low の変化で処理します。押し続けている間の毎回の要求は避けていますが、
接点の揺れは別なので、必要なら一定時間の安定を待つ debounce を追加します。

要求は交渉結果を待つため、ISR では呼びません。実アプリでは次の固定 PDO が見つかったことと要求の成否を確認し、
失敗した場合は次の押下を待つか再試行するかを決めます。接続相手が変わると PDO の並びも変わるため、
index を永続的な「9 Vの番号」として保存しません。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbPd の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[CycleVoltages.ino](CycleVoltages.ino)
