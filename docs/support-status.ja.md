# 系列ごとの実装範囲

全系列を `Generic <SERIES>` として build できます。upload の「未実装」は、同梱の `ch32rv` が
書き込み手段を持たず、`capability-unsupported` で終了する系列です。
「実装済（未検証）」は、ch32rv が同じ family の手順で書き込みますが、その系列では実機で確認していないものです。
経路別、probe 別の対応は [upload-routes.ja.md](upload-routes.ja.md) を参照してください。

| 系列 | build | upload |
|---|---|---|
| CH32V002 | 実装済 | 実装済 |
| CH32V003 | 実装済 | 実装済 |
| CH32V004 | 実装済 | 実装済 |
| CH32V005 | 実装済 | 実装済 |
| CH32V006 | 実装済 | 実装済 |
| CH32V007 | 実装済 | 実装済 |
| CH32M007 | 実装済 | 実装済 |
| CH32V103 | 実装済 | 実装済 |
| CH32V203 | 実装済 | 実装済 |
| CH32V208 | 実装済 | 実装済 |
| CH32V303 | 実装済 | 実装済 |
| CH32V305 | 実装済 | 実装済 |
| CH32V307 | 実装済 | 実装済 |
| CH32V317 | 実装済 | 実装済 |
| CH32X033 | 実装済 | 実装済 |
| CH32X035 | 実装済 | 実装済 |
| CH32L103 | 実装済 | 実装済 |
| CH32M103 | 実装済 | 実装済（未検証） |
| CH32V205 | 実装済 | 実装済 |
| CH32V407 | 実装済 | 未実装 |
| CH32V467 | 実装済 | 未実装 |
| CH32X305 | 実装済 | 実装済（未検証） |
| CH32X315 | 実装済 | 実装済 |
| CH32M030 | 実装済 | 未実装 |
| CH32H415 | 実装済 | 未実装 |
| CH32H416 | 実装済 | 未実装 |
| CH32H417 | 実装済 | 未実装 |

## 製品ボード固有機能

CH32X315 の書込み対象は zero-wait Flash の 192 KiB です。
追加の nonzero-wait 領域を合わせた物理容量を upload.maximum_size に使用しません。
upload の実装と、Arduino の周辺 API・各接続経路の実機検証範囲は別です。

CH32H415 / H416 / H417 は V3F コアだけを使います（V5F は起動しません）。
イメージは flash に置き、リセット時に共有 SRAM（0x20100000 からの 512 KiB）へ全体を複製して実行します。
code・data・stack の合計がこの 512 KiB に収まる必要があるので、upload.maximum_size と maximum_data_size はどちらも 512 KiB です。
クロックは HSI×16 の 400 MHz（V5F は停止のまま）で、V3F と HCLK（F_CPU）は 100 MHz です。

**高速端子の電源（VIO18）に注意してください。** PA13〜PA15、PB0〜PB1、PB10〜PB14、PC0〜PC3、PC6〜PC12、
PD0〜PD7、PD9〜PD15、PE0〜PE1、PE7〜PE15、PF0〜PF5、PF11〜PF14 は、チップ内の LDO が作る VIO18 で動きます。

- 起動時の電圧は XO 端子のストラップで決まり、評価ボードでは 1.2 V（実測 1.36 V）です。
  そのため、これらの端子の High は 3.3 V になりません。
- 入力の絶対最大定格は VIO18 + 0.3 V です。どれも 5 V トレラントではありません。
  1.2 V のまま 3.3 V の信号をつなぐと、定格を超えます。
- 変えるには、ボードメニューの **VIO18 Supply**（既定は Hardware default）か、`CH32RV.setVio18Millivolts()`
  （1200 / 1800 / 2500 / 3300）を使います。メニューの値は `setup()` の前に設定されますが、それまでの間は
  ストラップの電圧のままです。
- VIO18 に 1.8 V の部品をつないでいる基板では、上げないでください。
- HSADC の入力（PC0〜PC3）で 0〜3.3 V を測るときは、VIO18 を 3.3 V にします（データシートでは、HSADC を
  使う端子の電源 = VREFP）。

| ボード | 実装範囲 |
|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | HID bootloader upload。UART / GPIO / ADC / I2C / SPI は Generic CH32V003 の実装を使用 |

ペリフェラル単位の実装範囲は [peripheral-support.ja.md](peripheral-support.ja.md) を参照してください。
