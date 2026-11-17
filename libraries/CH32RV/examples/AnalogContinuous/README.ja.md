# AnalogContinuous

[English](README.md)

arduino-esp32 の連続 ADC の API（`analogContinuous()`、`analogContinuousStart()`、`analogContinuousRead()`）で、ADC が自分で `A0` を毎秒 10,000 回変換し、100 回のフレームごとに平均します。ESP32 と同じくコアが宣言するので、include は要りません。

## 期待する結果

10 ms ごとに `ADC_CONTINUOUS pin=64 raw=2047 mV=1650` のような行が出ます。一番新しいフレームの平均で、生の値（12 ビット、`analogContinuousSetWidth(12)`）と mV です。

## API と改造時の注意

- `analogContinuous(pins, count, conversions_per_pin, sampling_freq_hz, callback)` で設定します。ADC1 の端子 16 本まで、フレーム、速さ（全端子あわせた 1 秒あたりの変換の数。コアが取ったタイマーの刻みごとに端子を 1 巡。タイマーのクロックを整数で割れる範囲で正確）、フレームが埋まるたびに割り込みで呼ぶ関数。ADC1 の端子でない、トリガのタイマーに空きが無いときは false です。
- `analogContinuousRead(&result, timeout_ms)` は最大 `timeout_ms` フレームを待ち、一番新しいフレームの端子ごとの平均を返します。mV は `analogContinuous()` を呼んだときに（Vrefint で）測った電源電圧で換算します。Vrefint の無い石では 3300 mV。
- ADC に減衰器は無いので、`analogContinuousSetAtten()` は受け付けて何もしません。`analogContinuousDeinit()` はタイマーを返し、ADC を `analogRead()` に戻します。その間 `analogRead()` は使えません。
- 標本そのもの、端子の縁のトリガ、自走の ADC は [CH32RVAdcStream](../../../CH32RVAdcStream/README.ja.md) で。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
