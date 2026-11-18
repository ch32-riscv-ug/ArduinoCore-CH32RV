# CH32RVComparator

[English](README.md)

チップ内蔵のコンパレータ（CMP）を、入力と出力の端子を指定して使います。

```cpp
#include <CH32RVComparator.h>

CH32RVComparator cmp(1);                // CMP1

void onEdge() { /* 出力が変わった */ }

void setup() {
  cmp.setHysteresis(25);                // mV。系列が持つ段階のうち近いもの、0 で切る
  cmp.begin(PA2, PB1, PB12);            // +、-、出力（その CMP の端子。variant の CH32RV_CMP_PADS）
  attachInterrupt(digitalPinToInterrupt(PB12), onEdge, CHANGE);
}

void loop() {
  int above = cmp.read();               // 1: + が - より高い
}
```

- `read()` は、状態ビットのある系列（H41x）はそれを、ほかの系列は出力端子の値を読みます。出力端子を使わず
  （`outPin` -1）状態ビットも無い系列では -1 です。
- 出力の変化は、出力端子の `attachInterrupt()` で受けます。
- 入力の端子はスケッチが設定したまま（アナログ入力か、GPIO で駆動するか）。出力の端子は CMP のものに
  なり、`end()` で入力に戻ります。
- 内部の入力: `CH32RVComparator::OPA_OUTPUT` を + に（V205: 同じ番号の OPA の出力、H41x: OPA1 の出力）、
  `CH32RVComparator::REFERENCE` を - に（H41x: 電源の 1/4・2/4・3/4、`setReference(quarters)`、既定 2/4）。

## 対象

| 系列 | CMP | 端子（+ / - / 出力） | ヒステリシス |
|---|---|---|---|
| V007 / M007 | 1 | PC5・PB3・PD2 / PC2・PD5・PA6 / PC0 | 24 mV |
| V205 | 2 | CMP1: PA2・PB2 / PB1・PB3 / PB12、CMP2: PB8・PA8 / PA9・PB11 / PA12 | 12.5・25・50 mV |
| L103 / M103 | 3 | CMP1: PA2・PB2 / PB1・PB3 / PB0・PB10、CMP2: PB8・PB6 / PB3・PB7 / PB5・PB11、CMP3: PB5・PB10 / PB3・PB11 / PB4・PB12 | 15 mV |
| X035 / X033 | 3 | CMP1: PC19・PA0 / PC3・PA23 / PA1、CMP2: PA12・PA11 / PA22・PC3 / PB2、CMP3: PA13・PA14 / PA2・PC3 / PB3 | あり（値は RM に無い） |
| H41x | 1 | PB0・PB2 / PB1・PC4・PA5（DAC2） / PA6・PA8・PB12・PC5・PE6・PE12・PE15 | 10・20・30 mV |

端子と選択の値は各系列の RM から（tools/generate/opa/cmp_select.csv）。M030 のコンパレータ（モーター段）は
対象外です。V205 の 4 bit の DAC を - にする使い方と、タイマーへの出力・ブレーキ・起床はまだありません。

## 試験

selfapi::test_comparator: 板の触らない端子を除いた最初の + / - を GPIO で駆動し、`read()` と出力端子の割り込み
（2 回の変化）を見る。内部基準のある H41x は、+ を基準（電源の半分）とも比べる。V205・X035・H417 で確認。
