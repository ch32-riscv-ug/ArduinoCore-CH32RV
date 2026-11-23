# QspiFlashInfo

[English](README.md)

QSPI のフラッシュ（W25Q・GD25Q など）の JEDEC ID を読み、先頭の 16 バイトを 1 線・2 線・4 線の読み出し命令で読み比べます。

## 必要なもの

- QSPI のある系列（CH32V205、CH32H41x）のボード
- QSPI のフラッシュ（WP / HOLD の端子を IO2 / IO3 として使う 4 線対応のもの）

## 配線

| ボード（V205） | ボード（H41x） | フラッシュ |
|---|---|---|
| PA12 | PB2 | CLK |
| PB0 | PB6 | /CS |
| PA2 | PD11 | DI（IO0） |
| PA3 | PD12 | DO（IO1） |
| PA0 | PD14 | /WP（IO2） |
| PA1 | PD13 | /HOLD（IO3） |

CH32H416 は QSPI2 の端子だけなので、SCK PF6・/CS PF7・IO0〜3 PF8・PF9・PF10・PE15 です。端子は variant の `CH32RV_QSPIn_PADS` の中なら変えられます。

## 手順と期待する結果

`QSPI at 9600000 Hz` のような実際の速さと、`JEDEC ID EF 40 17`（W25Q64 の場合）を表示します。続く 3 行の 16 バイトは、同じ番地を 3 つの命令で読んだものなので同じになります。

## うまくいかないとき

- `begin() failed`: 端子がその系列の QSPI の組み合わせではありません。
- ID が `FF FF FF` か `00 00 00`: 配線、電源（3.3 V）、/CS を確かめてください。
- 4 線の行だけ違う: フラッシュの QE ビット（ステータスレジスタ 2）が立っていません。

## 実行前の準備

出力はハードウェア `Serial` の 115200 baud・8N1 です。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
