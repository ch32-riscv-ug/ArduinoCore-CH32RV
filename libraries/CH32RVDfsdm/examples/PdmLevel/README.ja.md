# PdmLevel

[English](README.md)

PDM マイクの音量です。DFSDM が CKOUT から約 2 MHz を与え、データを DATIN1 で受け、sinc3・1/64 で 31.25 kHz のサンプルにします。100 ms ごとに RMS とピークを棒つきで表示します。

## 必要なもの

- CH32H41x のボード
- PDM マイク（MEMS。CLK・DATA と、エッジを選ぶ L/R か SELECT）

## 配線

| マイク | ボード |
|---|---|
| CLK | PE9（DFSDM の CKOUT） |
| DATA | PD6（DFSDM の DATIN1） |
| L/R（SELECT） | GND（立ち上がりでデータ） |
| VDD / GND | 3.3 V / GND |

variant の `CH32RV_DFSDM_PADS` の CKOUT と DATIN0 / DATIN1 の端子なら変えられます。

## 手順と期待する結果

`clock 2000000 Hz, 31250 samples/s, full scale 262144` と表示し、100 ms ごとに 1 行出します。マイクの近くで話すと棒が伸びます。

## うまくいかないとき

- `begin() failed`: その端子はその役割の DFSDM の端子ではありません。
- 値が +-full scale のまま: DATA が High か Low に張り付いています（マイクが無い、電源が無い）。
- 何も動かない: L/R を High にしたマイクは立ち下がりで送ります。`begin()` の前に `mic.setFallingEdge(true)` を呼んでください。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
