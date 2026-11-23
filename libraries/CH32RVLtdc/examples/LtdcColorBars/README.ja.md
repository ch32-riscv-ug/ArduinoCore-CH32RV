# LtdcColorBars

[English](README.md)

LTDC で 480 x 272 の RGB パネル（16 ビット、RGB565）に 8 本の色の帯（白・黄・水色・緑・赤紫・赤・青・黒）を出します。

## 必要なもの

- CH32H417 のボード（H415 / H416 は LTDC の端子が少なく、スケッチの端子をそのまま使えません）
- 16 ビット RGB（DE 方式）の 4.3 インチ 480 x 272 のパネル

## 配線

| 信号 | 端子 |
|---|---|
| CLK / HSYNC / VSYNC / DE | PB14 / PA6 / PA4 / PE13 |
| R3〜R7 | PB0、PA5、PC0、PB1、PD4 |
| G2〜G7 | PD15、PE11、PB10、PB11、PC7、PB15 |
| B3〜B7 | PD7、PE12、PD5、PA14、PB9 |

パネルの残りの色の線（R0〜R2、G0〜G1、B0〜B2）は GND か、パネルの説明に従ってつなぎます。端子は variant の `CH32RV_LTDC_PADS` のその役割の端子なら変えられます。

## 手順と期待する結果

`LTDC at 10000000 Hz pixel clock` と表示し、パネルに縦の 8 本の帯が出ます。

## うまくいかないとき

- `a pin is not the LTDC's for that role`: その端子はその役割の LTDC の端子ではありません。
- 帯が流れる・ちらつく: パネルのタイミング（同期・ポーチ）と、ピクセルクロックの極性（`clockInverted`）を確かめてください。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
