# OnePulse

[English](README.md)

タイマーのワンパルスモード（`CH32RVOnePulse`）で正確な幅と遅れの 1 発のパルスを、出力比較（`CH32RVOutputCompare`）で矩形波を出します。どちらも時間はタイマーが計るので、CPU が何をしていてもパルスはタイマーの 1 カウントまで正確です。

## 配線

動かすだけなら配線は要りません。端子はスケッチが選び、起動時に表示します（`Pulses on PA6: ...`、`500 Hz square on PA7`）。ロジックアナライザかオシロスコープで見てください。

## 手順と期待する結果

100 ms ごとに、`fire()` の 50 µs 後から 20 µs、パルスの端子が High になります。2 つ目の端子は 1000 µs の周期の 250 µs で反転し、500 Hz・duty 50 % の矩形波になります。

## API と改造時の注意

- `fire(delayMicros, widthMicros)` はパルスを今始めます。前のパルスが続いている間（`busy()`）は false です。`begin(pin, HIGH, triggerPin, RISING)` にすると、同じタイマーの別の端子（チャネル 1 か 2）のエッジでハードがパルスを始めます。
- `CH32RVOutputCompare::set(mode, atMicros)` の mode は `CH32RVOutputCompareMode::Active`・`CH32RVOutputCompareMode::Inactive`・`CH32RVOutputCompareMode::Toggle`・`CH32RVOutputCompareMode::ForceActive`・`CH32RVOutputCompareMode::ForceInactive`・`CH32RVOutputCompareMode::Timing` です。`onMatch()` で一致ごとにコールバックを呼べます。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
