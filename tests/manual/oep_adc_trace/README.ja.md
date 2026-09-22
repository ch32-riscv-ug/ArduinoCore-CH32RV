# oep_adc_trace — analogRead() の端点と「無い channel」の見分け方（worklist P3 行 3）

`adc_probe` sketch は console から `ADC <pin> [n]`（analogRead の min/max/mean）、`ADCSEQ <pin> [n]`（連続 n 回の先頭/末尾 8 値）、
`CH <channel> [n]`（pin map を通さない regular channel 変換、15 = VREFINT）を受ける。
P4 側は `fixture.gpio` で各 ADC pad（E143 配線の PA0〜PA7）を push-pull low / high / floating にして、DUT が rail を読めるかを見る。
この fixture に校正済みの中点電源は無いので、直線性は範囲外（P4 内部 pull を基準電圧にしない、worklist の方針どおり）。

```
uv run tests/manual/oep_adc_trace/oep_adc_trace.py [--order PA3,PA7]
```

## 2026-09-22 の結果（CH32X035F8U6、48 MHz、10-bit 既定）

| pin | ch | P4 low | P4 high | floating | 判定 |
|---|---:|---:|---:|---:|---|
| PA0 / PA1 / PA2 / PA4 / PA5 / PA6 | 0/1/2/4/5/6 | 1（max 3） | 1007〜1008（max 1011） | 700〜890（pin 依存） | OK。rail を読む、16 回のばらつき ≤ 4 count |
| PA3 | 3 | 837 | 790 | 745 | **BAD**。pin に追従せず、単調減衰 |
| PA7 | 7 | 676 | 638 | 602 | **BAD**。同上 |
| （VREFINT） | 15 | — | — | — | **無い**。PA0 を high にして読んだ直後は 997、low の直後は 2（期待は両方 ≈372） |

中点（概略）: P4 の pull-up と pull-down を同時に掛けると線は 1.46〜1.49 V（wch-protocols E087、pull-down がやや強い）。この刺激で PA0/1/2/4/5/6 は **451〜463**（期待 ≈455 @3.3 V）。校正基準ではないが、rail 以外の値が rail と整合して読めている。

### 読み方: 「無い channel」は 0 を返さない

データシート（CH32X035DS0 注 1、device-data errata `x035-adc-ch-i2c-unavailable`）は「ロット番号の下から 5 桁目が 0 の製品では
ADC ch3/7/11/15 と I2C が使えない」と書く。この F8U6 では ch3 / ch7 / ch15 がそれに当たる（ch11 = PC1 は QFN20 に pad が無く未確認）。

無い channel を変換すると、値は 0 でも定数でもなく **直前の変換が sample-and-hold ノードに残した電荷**を読み、1000 回あたり数 % ずつ減衰する
（`ADCSEQ PA3 1000`: 334 → 307、PA7: 194 → 172）。pin を high/low に振ってもわずかに（+100 count 級）揺れるだけで DC には追従しない。
このため **1 回読みは正しく見える**: 最初に VREFINT を 16 回だけ読んだ時は 406（≈372 の期待に近い）が出たが、直前に PA7 を読んだ残留だった。
runner は PA0 を両 rail で前置してから ch15 を 1000 回読み、「前置に追従する」ことで無いと判定する。以前 PA3 が 1008 を返した回も、
直前の PA2 high 読みの残留で説明できる。

### I2C 条項との食い違い

同じ errata は I2C も使えないと書くが、この個体の I2C（route 2、PC16/PC17）は `oep_i2c_trace` で 10/100/400 kHz の write/read/repeated START が
通っている。ADC 条項と I2C 条項が同じロット条件で必ず一緒に来るとは限らない。**パッケージ刻印のロット番号は未確認**（読み取りは目視）。

### core への含意

- variant の pin map は series 共通なので PA3/PA7 は `A3`/`A7` のまま。ロット依存を core で隠すことはできない。
- fixture の ADC 試験は ch3/7/11/15 を避ける（[upload-and-fixture](../../../docs/upload-and-fixture.ja.md) の errata 節どおり）。
- 「ADC が読めているか」の自己診断には VREFINT を **前置してから** 読む（rail 追従なら無い）。単発の値で判断しない。
