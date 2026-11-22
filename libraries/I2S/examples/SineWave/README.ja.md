# SineWave

[English](README.md)

I2S の DAC に 441 Hz の正弦波を流します。16 bit・ステレオ・44.1 kHz で、左右に同じ音を出します。

## 必要なもの

- I2S2 のある系列のボード（CH32V30x、CH32V4x7、CH32H41x）
- I2S の DAC かアンプ（MAX98357A、PCM5102A、UDA1334A など）とスピーカーかイヤホン

## 配線

| ボード | DAC |
|---|---|
| BCLK（PB13） | BCLK / BCK |
| WS（PB12） | LRC / LRCK / WS |
| DOUT（PB15） | DIN |
| 3.3 V、GND | VIN、GND |

CH32H416 は PB12〜PB15 が無いので BCLK PD3・WS PB4・DOUT PC3 です。端子は I2S2 のものなら変えられます。使える端子は variant の `CH32RV_I2S2_PADS`（I2S3 は `CH32RV_I2S3_PADS`）にあります。H41x はどの組み合わせでも、V30x・V4x7 は同じ route のものだけです。

## 手順と期待する結果

書き込むと `I2S at 44102 Hz (asked 44100)` のように実際の速さを表示し、441 Hz の音が鳴り続けます。5 秒ごとに `underruns 0` を表示します。

## うまくいかないとき

- `error 2`: 端子がその系列の I2S の組み合わせではありません。
- `error 1`: 速さを分周器で作れません（16 bit で SYSCLK / 16352 より遅いもの。H41x の 400 MHz では 24.5 kHz 未満）。
- 音が出ない: DAC の電源と、WS と BCLK の取り違えを確かめてください。MAX98357A は SD 端子が Low だと止まっています。
- `underruns` が増える: `loop()` に時間のかかる処理があり、DMA のバッファーが空になっています。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
