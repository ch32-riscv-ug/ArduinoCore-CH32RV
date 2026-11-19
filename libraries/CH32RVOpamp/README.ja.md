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

| 系列 | OPA | 端子（+ / - / 出力） | PGA |
|---|---|---|---|
| V003 | 1 | PA2・PD7 / PA1・PD0 / PD4 | なし |
| V00x（V005〜V007・M007） | 1 | PA2・PD7・PD3・PD1 / PA1・PD0 / PD4・PA5 | 4〜32 倍 |
| V20x / V30x | 2 / 4 | 各 OPA に 2 つずつ（CH0P・CH1P など） | なし |
| V205 | 2 | OPA1: PB5・PB0・PB12 / PB11・PA6 / PB1・PA3、OPA2: PB14・PA7 / PB10・PA5 / PA4・PA2 | OPA1 8〜64 倍、OPA2 4〜32 倍 |
| V4x7 | 1 | PB12・PA6 / PB13・PA5 / PB1・PA7 | 4〜32 倍 |
| L103 / M103 | 1 | PB15・PB0・PB14・PA7・PA0・PA6 / PB11・PA6・PB10・PA5・PA1・PA7 / PA3・PB1・PA2・PA4・PB0 | 8〜64 倍 |
| X035 / X033 | 2 | OPA1: PB0・PB8・PB4 / PA6・PB6 / PA3・PB5、OPA2: PA7・PB3・PB7 / PA5・PB1 / PA4・PA2 | 4〜32 倍 |
| H41x | 3 | OPA1: PB0・PA6 / PB1・PA7 / PC4・PA5、OPA2: PE9・PF11 / PE8・PF12 / PE7・PB1、OPA3: PC2・PA2 / PC3・PA3 / PA0・PA4 | 8〜64 倍 |

端子と選択の値は各系列の RM から（tools/generate/opa/opa_select.csv。V20x / V30x は device-data の番号）。
小さいパッケージに無い端子は、その variant の CH32RV_OPA_PADS に入りません。M030 の OPA（モーター段の電流検出）は
対象外です。

## 試験

selfapi::test_opamp: 板の触らない端子を除いた最初の + / - / 出力で、+ と - を GPIO で駆動し、開ループの出力を
analogRead で読む。PGA のある系列は最低の倍率でも同じく。V003（試験用の板では + の端子が全部触らない端子）と
L103 以外の、V006・V203・V205・V407・X035・H417 で確認。
