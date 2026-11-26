# TimerAlarm

[English](README.md)

arduino-esp32 のタイマー API（`timerBegin()`、`timerAttachInterrupt()`、`timerAlarm()`）で、ハードウェアのタイマーのアラームで毎秒関数を動かします。ESP32 と同じくコアが宣言するので、include は要りません。

## 手順と期待する結果

1 秒ごとに `Alarm 1, count at it 0, 3 ms into the period` のように出ます。アラームでカウントは 0 に戻る（autoreload）ので、コールバックが読むカウントはそこから数カウント先です。`loop()` の `timerReadMillis()` は、表示した時点で周期がどこまで進んだかです。

## API と改造時の注意

- `timerBegin(frequency)` は空いているタイマーを `frequency` で数えさせ、動かし始めます（タイマーのクロックを整数で割った速さ。`timerGetFrequency()` で分かる）。カウントは 64 ビットです。
- `timerAlarm(timer, value, autoreload, reload)` は `value` でコールバックを呼びます。autoreload ならそこでカウントが `reload` になり、繰り返します。`timerWrite()`・`timerRead()`・`timerStop()` / `timerStart()`・`timerEnd()` は ESP32 と同じです。
- コールバックはタイマーの割り込みで動きます。周期の関数がいくつもあるなら、[Ticker](../../../../Ticker/README.ja.md) ライブラリが 1 つのタイマーを分け合います。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../../docs/upload-routes.ja.md)も参照してください。
