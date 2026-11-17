# EdgeTimes

[English](README.md)

タイマーの入力捕捉（`CH32RVInputCapture`）で信号のエッジごとの時刻を取り、その間の High と Low の時間を表示します。エッジの瞬間のカウントはハードが押さえるので、割り込みが遅れても時間は 1 カウント（ここでは 1 µs）まで正確です。

## 操作

出荷のまま（`SELF_TEST 1`）だと、測る端子に `tone()` で 1 kHz の矩形波を出し、その端子自身を測ります。配線は要りません。0.5 秒ごとに `high 500 us  low 500 us  high 500 us ...` のように出ます。

自分の信号を測るときは `SELF_TEST 0` にし、起動時に表示される端子（`Timing pin ...`）へ入れます。端子の電圧の範囲を守り、GND を共通にしてください。線が動かないと `No edges: the line is still` と出ます。

## API と改造時の注意

- `begin(pin, CHANGE, 1000000, INPUT_PULLDOWN)` は両方のエッジを 1 MHz で取ります。`RISING` か `FALLING` だけにすると、同じ向きのエッジの間（周期）になります。`tickHz` を変えると分解能が変わります。
- `read()` はキューのエッジを古い順に返します（8 個まで）。`onEdge()` にコールバックを渡すと、代わりにエッジごとにタイマーの割り込みから呼ばれます。
- 端子は、`tone()` と別のタイマーのチャネルから自動で選びます（どのチャネル・経路でも）。[ライブラリの README](../../README.ja.md) を参照してください。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
