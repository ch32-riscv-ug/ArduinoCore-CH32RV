# AnalogInputToPwm

[English](README.md)

可変抵抗でLEDのPWM dutyを調節し、別々の`AnalogRead`と`Fade`の内容をつなぎます。

## 配線と操作

可変抵抗の両端を基板のアナログ入力に適合する電源とGNDへ、中央の摺動端子を`INPUT_PIN`（`A0`）へつなぎます。抵抗値はADC入力に合うものを選び、目安として10 kΩ程度から試せます。摺動端子の電圧を入力の許容範囲内に保ちます。アナログ用の電源がUSBの5 Vと同じとは限りません。

`PWM_PIN`（`PA1`）から適切な抵抗とLEDを介してGNDへつなぎます。アナログ入力・UART・USBと重ならない、実際にPWM対応の端子を使います。モーターなどの電力負荷を直接駆動しないでください。基板のLEDがLowで点灯する配線なら、dutyと明るさの向きは反転します。

つまみを電源側へ回すと読み取り値とdutyが増え、明るさが変わります。UARTは毎秒2回、ADCとPWMの値を表示します。値が揺れ続けたり端の値に張り付いたりする場合は、両端と摺動端子の配線を確認します。

## APIと改造時の注意

`analogReadResolution(12)`で読み取りを0～4095、`analogWriteResolution(8)`で書き込みを0～255にします。両者は独立した数値範囲です。`raw * 255 / 4095`は32 bitで中間計算します。これは範囲の変換であり、物理的な電圧への換算ではありません。ADCのbit数を増やして要求しても精度が増えるわけではありません。

`millis()`の差で10 msごとに読み、表示だけ頻度を落とします。`analogWrite()`はdutyを変えるもので、DACの電圧出力にはなりません。ADCの小さな雑音で明るさが変わる場合は、用途に合わせて平均化やhysteresisを追加します。周波数とタイマー共有は[PwmFrequency](../PwmFrequency/README.ja.md)を参照してください。

## ボードとUARTの準備

ボードと正確なPart Numberを選び、各端子を回路図と照合します。Genericの別名はvariantの定義であり、基板コネクタの表示と同じとは限りません。UART・書き込み／debug・ほかの周辺回路との端子競合を避けます。生成された`sketch.yaml`のprofileはビルド条件であり、配線の確認結果ではありません。

ログはハードウェア`Serial`の115200 baud・8N1です。アダプタRX←ボードTX、アダプタTX→ボードRX、共通GNDで接続し、I/O電圧を合わせます。このUARTはUSB HIDとは別で、基板のUSB端子から自動的に見えるわけではありません。

[ビルド規則](../../../../docs/examples-build-rules.ja.md)・[書き込み経路](../../../../docs/upload-routes.ja.md)・[サンプル一覧](../../../../docs/examples.ja.md)も参照してください。
