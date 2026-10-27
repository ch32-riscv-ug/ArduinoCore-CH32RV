# PulseMeasure

[English](README.md)

タイマーの PWM 入力モードで、信号の周波数・周期・パルス幅を測ります。数えるのはタイマーで、スケッチは 0.5 秒ごとに最新の 1 周期を読むだけです。割り込みも、端子のサンプリングも使いません。

## 操作

出荷のまま（`SELF_TEST 1`）だと、測る端子に `tone()` で 1 kHz の矩形波を出し、その端子自身を測ります。配線は要りません。0.5 秒ごとに `1000 Hz, period 1000 us, high 500 us, duty 50 %` のように出ます。`TEST_HZ` を変えると、結果もそれに合わせて変わります。

自分の信号を測るときは `SELF_TEST 0` にし、起動時に表示される端子（`Measuring PA6 ...`）へ入れます。端子の電圧の範囲を守り、GND を共通にしてください。線が止まると `No signal: held high` / `held low` と出ます。

## API と改造時の注意

- `begin(pin, MIN_HZ, INPUT_PULLDOWN)` の `MIN_HZ` は、測る最も遅い信号です。分解能もこれで決まります。速い信号を細かく測るなら上げ、遅い信号を測るなら下げます。`MIN_HZ` より遅い信号は測れず、`stalled()` が true になります。
- 端子は、`tone()` と別のタイマーのチャネル 1 か 2 から自動で選びます。自分で決めるときは、その系列のチャネル 1 / 2 の端子を `pin` に入れます（[ライブラリの README](../../README.ja.md)）。
- 浮動小数点を使わずに計算しています。V003 のフラッシュに入れるためです。`Pulse::frequency()` と `duty()` は float を返します。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
