# SpiAsyncDisplay

[English](README.md)

ST7789（240×240、16 bit カラー）の表示器に、帯の模様を描き続けます。8 行ぶんの buffer を 2 本持ち、一方を
`SPI.transferAsync()`（DMA）で送っている間に、もう一方に次の 8 行を描きます。1 画面の時間を `Serial` に出します。

## 配線と操作

表示器の SCK・MOSI を SPI1 の既定の端子に、CS を `PA4`、DC を `PA3`、RST を `PA2` につなぎ、電源と GND を共通にします
（MISO は使いません）。端子は スケッチの先頭の定数で変えられます。表示器の電圧（多くは 3.3 V）と、バックライトの
つなぎ方は、その基板の資料で確かめてください。

## APIと改造時の注意

- `transferAsync()` は送り始めるとすぐ戻ります。`finishedAsync()` が `true` を返すまで、その buffer を書き換えず、
  バスでほかの転送をしません。この例は、描き終えた buffer を送る前に、前の転送の終わりを待ちます。
- `transferAsync()` が `false` のとき（その SPI の DMA の channel をほかの利用者が持っている、SPI がソフトウェア）は、
  `writeBytes()` で同じ buffer をその場で送ります。結果の画面は同じで、描く時間と送る時間が重ならないだけです。
- 画素は表示器の byte 順（上位 byte が先）で buffer に置きます。
- 24 MHz を頼んでも、SPI の分周で実際の速さは下がることがあります（[SPI](../../README.ja.md)の「知っておくとよいこと」）。
- LVGL の flush で使うときは、flush の中で `transferAsync()` を呼び、`lv_display_set_flush_wait_cb()` で
  `finishedAsync()` を待つと、LVGL が次の部分を描く時間と重なります。

## 実行前の準備

ボードと正確な Part Number を選び、スケッチの端子を回路図と照合します。ログはハードウェア `Serial` の 115200 baud・8N1 です。
生成された `sketch.yaml` にビルド profile があります。ビルド成功は配線や実機動作の確認とは別です。
[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
