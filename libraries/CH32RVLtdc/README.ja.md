# CH32RVLtdc

[English](README.md)

CH32H41x の LCD-TFT コントローラー（LTDC）です。RAM のフレームバッファから RGB パネルを駆動します（ピクセルクロック・HSYNC・VSYNC・DE と最大 24 本の色の線、背景色の上に 2 枚のレイヤーを合成）。Arduino・arduino-esp32 に対応するものはありません（arduino-esp32 は RGB パネルを esp_lcd に任せています）。

```cpp
#include <CH32RVLtdc.h>

CH32RVLtdc lcd;

void setup() {
  CH32RVLtdcTiming t;                              // 480 x 272 のパネル
  t.width = 480; t.height = 272;
  t.hsync = 4; t.hbp = 43; t.hfp = 8;
  t.vsync = 4; t.vbp = 12; t.vfp = 8;
  t.pixelHz = 10000000;
  const int r[8] = {-1, -1, -1, PB0, PA5, PC0, PB1, PD4};     // R0 .. R7。-1 は使わない線
  const int g[8] = {-1, -1, PD15, PE11, PB10, PB11, PC7, PB15};
  const int b[8] = {-1, -1, -1, PD7, PE12, PD5, PA14, PB9};
  lcd.setSignalPins(PB14, PA6, PA4, PE13);          // CLK, HSYNC, VSYNC, DE
  lcd.setColorPins(r, g, b);
  lcd.begin(t);
  uint16_t *fb = (uint16_t *)malloc(480 * 272 * 2); // RGB565
  lcd.layer(1, fb, CH32RVLtdcFormat::Rgb565, 0, 0, 480, 272);
}
```

| 関数 | 意味 |
|---|---|
| `setSignalPins(clk, hsync, vsync, de)` | 信号の端子（`begin()` の前に） |
| `setColorPins(r[8], g[8], b[8])` | 色の端子。ビット 0 から。使わない線は -1 |
| `begin(timing)` / `end()` | `CH32RVLtdcTiming` でパネルを始める／止めて端子を入力に戻す |
| `layer(n, fb, format, x, y, w, h, alpha = 255)` | レイヤー 1 か 2。バッファを (x, y) に w x h 画素で。null で隠す。次のフレームから |
| `setBackground(0xRRGGBB)` | レイヤーの下の色 |
| `pixelClock()` | 実際のピクセルクロック（H41x は SYSCLK / 1 .. 64、V4x7 は 500 MHz / 1 .. 63） |
| `line()` | 今送っている行（垂直帰線の間は負） |

`CH32RVLtdcTiming`: `width` / `height`、`hsync` / `hbp` / `hfp`、`vsync` / `vbp` / `vfp`、`pixelHz` と極性 `hsyncHigh`・`vsyncHigh`（false でアクティブ Low）、`deHigh`（true）、`clockInverted`（false で立ち上がりでデータ）。形式は ARGB8888、RGB888、RGB565、ARGB1555、ARGB4444、L8、AL44、AL88（`CH32RV_LTDC_*`）です。

- 端子は variant の `CH32RV_LTDC_PADS` のその役割の行でなければなりません。1 つの端子が 2 つの役割を持つことがあり（PB14 は CLK と G0）、役割は渡した場所で決まります。
- 表示中のフレームバッファは解放しないでください（LTDC が自分の DMA で読みます）。480 x 272 の RGB565 は 255 KiB です。
- スケッチのレイヤーが 1 枚も無いときも背景が出ます。透明なレイヤー 1 を全面に置いて代わりにしています（LTDC はレイヤーの窓の外で色を送らないため）。ヒープに `width` 画素の RGB565 を 1 行分使います。
- CH32H415 / H416 は LTDC の端子が少なめです。CH32V4x7 の LTDC はレイヤーが 1 枚（`layer(2, ...)` は false）で、ピクセルクロックは Ethernet の PLL（HSE x 20 で 500 MHz）/ 1〜63 です。PLL は `begin()` が起こし、止めません（Ethernet の MAC も使うため）。ビルドだけです（HSYNC・VSYNC がベンチの線に無い）。

## 例

- [LtdcColorBars](examples/LtdcColorBars/README.ja.md): 480 x 272 のパネルに 8 本の色の帯。

## 試験

periph::test_ltdc: HSYNC・VSYNC・DE と R・G の 1 本ずつを取り込み、WireSkein で同期の周期とデューティー、DE と色の線のずれ、もう一方の色が Low のままかを判定します（背景だけ、色のレイヤー、背景の上の透明なレイヤー）。H417 で合格（2026-11-23）。パネルはつないでいません。H415 / H416 はビルドだけです。
