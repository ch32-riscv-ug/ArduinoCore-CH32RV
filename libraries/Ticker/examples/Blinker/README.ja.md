# Blinker

[English](README.md)

arduino-esp32 の Ticker の例を、そのままの形で動かします。1 つ目の Ticker が LED を点滅させ、2 つ目が 5 秒ごとに点滅を入り切りし、3 つ目が 30 秒後に 1 回だけ点滅を遅くします。3 つとも 1 つのハードウェアのタイマーを共有します。

## 配線

`LED`（PA1）に、電流制限の抵抗をつけた LED をつなぎます。

## 手順と期待する結果

LED が 5 秒間速く（0.1 秒ごとに）点滅し、5 秒間消え、これを繰り返します。30 秒たつと、点滅が 0.5 秒ごとに遅くなります。

## API と改造時の注意

- `attach(seconds, callback)` は周期ごとに、`once(seconds, callback)` は 1 回だけ呼びます。`attach_ms()` / `attach_us()`、`once_ms()` / `once_us()` はミリ秒とマイクロ秒です。`detach()` で止め、`active()` で動いているか分かります。
- コールバックはタイマーの割り込みで動きます（ESP32 ではタスク）。短くし、`Serial` や `delay()` は `loop()` で使ってください。
- どれかの Ticker が動いている間、タイマーを 1 つ使います（1 MHz の `timerBegin()`）。analogWrite()・tone()・Servo・ライブラリがタイマーを全部使っていると、`attach()` は何もせず、`active()` は false です。

## 実行前の準備

生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
