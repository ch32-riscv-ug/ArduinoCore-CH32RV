# PiocPulses

[English](README.md)

スケッチの `pulses.S` に書いた PIOC のプログラムが、コマンド n ごとに IO0 へ n 個のパルスを出し、0x5A を返します。

## 必要なものと配線

PIOC のある石（X035 / X033、V205、H41x）。IO0 の端子（スケッチの `IO0_PIN`。CH32H417 は PA0、CH32X035 は PC7、CH32X033 は PC18（SWD と兼用）、CH32V205 は PB10）をロジックアナライザかオシロスコープに。

## 操作と期待する結果

`Serial` に `1 pulses -> 0x5A, commands so far 1` のように 0.5 秒ごとに出ます。IO0 には 1〜8 個のパルスが出ます。
1 個の幅は PIOC の 4 サイクル、周期は 9 サイクルです（H417 の 100 MHz で High 40 ns・周期 90 ns、2026-11-19 にプローブで確認）。

## API と処理の流れ

`PIOC_IMAGE(pulses)` で `.S` のイメージを宣言し、`begin(PIOC_IMAGE_OF(pulses), IO0_PIN)` で PIOC の SRAM に写して走らせます。
`command(n)` が cmd.in に書くと、プログラムは `WAITB WB_DATA_MW_SR_1` から進み、n 個のパルスを出してから cmd.out に 0x5A を
書き、`SB_INT_REQ` を立てます。`waitDone()` がそれを待って cmd.out を読みます。d1 はプログラムが数えたコマンドの数です。

## 改造するとき

`pulses.S` の NOP を増やすとパルスが長くなります。IO1 も使うなら `begin()` の 3 つ目の引数に端子を渡し、プログラムで
`SB_PORT_OUT1` を動かします。

## 関連する文書

- [スケッチ](PiocPulses.ino)、[PIOC のプログラム](pulses.S)
- [CH32RVPioc](../../README.ja.md)
