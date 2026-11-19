# PiocPulseIn

[English](README.md)

PIOC で `pulseIn()` をします。`pulse_in.S` が IO0 の信号の 1 周期（High とそれに続く Low）を数えるので、メインのコアの割込み
（USB、`millis()`）に測定が揺らされません。0.5 秒ごとに High・Low の長さと周波数を `Serial` に出します。

## 必要なものと配線

PIOC のある石（X035 / X033、V205、H41x）。測る信号を IO0 の端子（スケッチの `IO0_PIN`。CH32H417 は PA0、CH32X035 は PC7、CH32X033 は PC18（SWD と兼用）、
CH32V205 は PB10）に。GND も共通に。信号は 3.3 V の振幅で。

## 操作と期待する結果

1 kHz・デューティ 25 % の信号なら、`high 250000 ns, low 750000 ns, 1000.0 Hz` のように出ます。信号が無ければ `no signal`。

## API と処理の流れ

- `command()` を送ると、プログラムは IO0 が Low になるのを待ち、立ち上がりから High の間と、続く Low の間を数えます。
- 数えるループは 1 回 8 サイクルです（桁上がりがあっても無くても同じ）。数は d1〜d3（High）と d4〜d6（Low）の 24 ビットで、
  `count * 8 / CH32RVPioc::clockHz()`（数 × 8 ÷ クロック） が長さです。48 MHz で 1 回 167 ns、最長約 2.8 秒。
- エッジが来ないとプログラムは待ち続けるので、`waitDone()` が時間切れになったら `begin()` し直して止めます。

H417・X035（48 MHz）・V205 で、プローブが出した 1 kHz 25 %・10 kHz 50 %・40 kHz 75 % の PWM を測り、High と Low が
プローブの答えと 0.5 % 以内でした（2026-11-19、pioc::test_pioc_pulse_in。判定は 2 % と 3 回分）。
