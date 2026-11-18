# CH32RVOpamp

[English](README.md)

チップ内蔵のオペアンプ（OPA）を、入力と出力の端子を指定して使います。

```cpp
#include <CH32RVOpamp.h>

CH32RVOpamp opa(1);                     // OPA1

void setup() {
  opa.begin(PB15, PB11, PA3);           // +、-、出力（その OPA の端子。variant の CH32RV_OPA_PADS）
}
```

- 帰還をつながなければ + と - の比較（開ループ）で、出力は高い方の電源側に振れます。出力を - に
  つなげばボルテージフォロワ、抵抗を通せば増幅になります。
- `beginPga(plus, gain, out)` は内蔵の帰還抵抗で増幅します（V4x7: 4・8・16・32 倍、- 端子は使わない）。
  PGA の無い系列やない倍率では false。
- 入力の端子はスケッチが設定したまま（アナログ入力か、GPIO で駆動するか）。出力の端子は OPA のものに
  なります。`end()` で OPA を止めます。
- `begin()` の出力を省くと、その OPA の最初の出力端子を使います。

## 対象

| 系列 | OPA | 端子 |
|---|---|---|
| V003 | 1 | + PA2 / PD7、- PA1 / PD0、出力 PD4 |
| V20x / V30x | 2 / 4 | 各 OPA に + / - / 出力が 2 つずつ |
| V4x7 | 1 | + PB12 / PA6、- PB13 / PA5、出力 PB1 / PA7。PGA あり |

V00x・V205・L103・M030・X035・H41x にも OPA があり、端子は CH32RV_OPA_PADS に入っていますが、
選択レジスタをまだ設定しないので `begin()` は false です。

## 試験

selfapi::test_opamp: + と - を GPIO で駆動し、開ループの出力を analogRead で読む（V203・V407 で確認。
V407 は PGA 4 倍も）。
