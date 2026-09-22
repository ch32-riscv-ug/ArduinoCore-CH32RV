# oep_gpio_matrix — X035 の全接続 pad を両側から駆動・観測する（worklist P3 行 1）

`gpio_probe` sketch を OEP probe 経由で転送し、E143 の pin map（X035 pad ↔ P4 GPIO）の 13 pin について、
X035 出力 → P4 読み、P4 駆動 → X035 の INPUT / INPUT_PULLUP / INPUT_PULLDOWN / OUTPUT_OPENDRAIN での読み、
EXTI RISING / FALLING / CHANGE の回数を `fixture.gpio` で測る。

```
uv run tests/manual/oep_gpio_matrix/oep_gpio_matrix.py [--pins PA0,PB3] [--settle 0.05]
```

除外: PB0/PB1（console USART4）、PC10/PC11（USB pad と結線、core が never-driven）、PC18/PC19（SWD）。

## 2026-09-22 の結果

最初の run（core 176f7c2）で 3 種の不良が出て、2 つは core の修正、1 つは fixture の癖と判明した:

| 現象 | 原因 | 対処 |
|---|---|---|
| EXTI が port B / C の pin で 0 回（PB3/PB11/PB12/PC14/PC15） | core が AFIO_EXTICR を STM32-F1 流（1 line 4 bit × 4 register）で書いていた。X035 は 1 line 2 bit・16 line/register（ch32-data `afio_x0`、M030/V00x も同じ） | `tools/generate` が variant ごとに `CH32_EXTICR_FIELD_BITS` / `_FIELDS_PER_REG` を出し、`wiring_interrupts.c` がそれで書く。修正後 10/10/20 |
| `OUTPUT_OPENDRAIN` で release しても P4 が low に引けない（全 pin） | X0 の GPIO block に汎用 open-drain が無い（CNF=01 は push-pull と同じ）。errata `x035-no-gpio-open-drain` | core が OD をエミュレート（release = floating input、low = push-pull low）。修正後 P4 が引ける |
| PB3/PB12 の INPUT_PULLDOWN idle が 1 | P4 GPIO13/14（SD card pad）が high 駆動後にゆっくりしか戻らない（500 ms で 0）。fixture 側 | 判定から除外 |
| PC14/PC15 の INPUT_PULLUP idle が 0 | USB PD の CC pin。`USBPD_PORT` の reset 値は 0x00030003（CC1/CC2 とも `CC_PD`=1）。ただし `CC_PD` を落としても `USBPD_CONFIG`=0 でも `AFIO_CTLR` の PD bit を変えても pin は上がらず、push-pull high は通る（≈5 kΩ 級の pull-down が線上に残る。PD PHY か P4 側 GPIO10/15/45 かは未決） | 未決。todo に記録 |

修正後: PA0〜PA7、PB3、PB11、PB12 は out / od / in / pullup / EXTI すべて OK。PC14/PC15 は pull-up idle 以外 OK。
