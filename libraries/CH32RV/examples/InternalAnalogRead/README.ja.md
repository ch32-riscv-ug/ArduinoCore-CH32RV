# InternalAnalogRead

[English](README.md)

ADC の内部チャネルを読みます。内部の基準電圧、それから求めた電源電圧、チップの温度の 3 つです。UART のほかに配線は要りません。端子を読む [AnalogRead](../AnalogRead/README.ja.md) と比べてください。

## 操作

2 秒ごとに 1 行出ます。CH32V307 なら、たとえば `Vref reading 381  VDD 3220 mV  die 22.2 C (typical constants, not calibrated)` です。指でチップを温めると、温度が数度上がります。温度センサの無い系列（V00x、X033 / X035、X305 / X315、M030）は `(no sensor)` と出ます。電源電圧を求められないときは `(unknown)` です。

## API と改造時の注意

- `analogReadVref()`: 内部の基準電圧を、今の分解能での `analogRead()` と同じ単位で返します。持たない系列では 0 です。
- `CH32RV.getVddMillivolts()`: 基準電圧の代表値から求めた電源電圧（mV）です。数パーセントの誤差があります。わからないときは 0 です。基準電圧の読みが 6 V を超える電源を意味するときも 0 にします（ベンチの CH32X035C8T6 は ADC_IN15 をほぼ 0 と読みます）。
- `analogReadTemp()`: データシートの代表値で換算した摂氏です。センサが無いときは NAN です。個体ごとに数度ずれます（CH32V103 は約 20 度高く出ます）。校正しない限り、絶対の温度ではなく、変化を見るのに使ってください。

1 回の読みは揺れるので、スケッチは 16 回の平均を取ります。CH32RVAdcStream を動かしている間は呼ばないでください。同じ ADC を使います。

## 実行前の準備

ボードと正確な Part Number を選びます。出力はハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。基板の USB 端子だけでこの UART が見えるとは限りません。

生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
