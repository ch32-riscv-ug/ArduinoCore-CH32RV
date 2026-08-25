# USBPD — PD充電器に電圧を頼む(sink)

```cpp
#include <USBPD.h>

if (USBPD.begin()) {
    while (!USBPD.ready()) { }              // 充電器の列挙を待つ
    for (uint8_t i = 0; i < USBPD.profileCount(); i++) {
        PDProfile p = USBPD.profile(i);     // 充電器が出せるもの
    }
    USBPD.request(9000);                    // 9 V ください
}
void loop() { USBPD.maintain(); }           // PPS契約の維持
```

## いまの状態

**フレームロジックは実装・検証済み、ハードウェアドライバは未実装です。**

| 層 | 状態 |
|---|---|
| Source Capabilitiesの解析、プロファイル選択、Requestの組み立て | 実装済み。hostのunit test(`tests/unit/test_pd_frames.py`)と実機の自己検査(`tests/sketches/basic/pd_selftest`)の両方で検証 |
| CC検出・送受信・交渉のstate machine | **未実装**。`begin()`は正直に`false`を返す(Wireのslaveと同じ規則: 受け付けて何もしないより、できないと言う) |

`begin()`の戻り値を見ているsketchは、ドライバが入った日にそのまま動きます。

## API

単位は**常にmVとmA**です。`request(9)`は9mVを頼むことになり、
該当プロファイルが無いので綺麗に失敗します。

| | |
|---|---|
| `begin()` / `end()` | 開始・停止。USBPDブロックの無い部品では`false` |
| `connected()` | CCで電源が繋がっている |
| `ready()` | 明示契約が成立した(PS_RDY)。以後`profile()`と`voltage()`が生きる |
| `profileCount()` / `profile(i)` | 充電器の広告の列挙。`[0]`は仕様上必ず5V固定 |
| `request(mV, mA=0)` | 電圧を頼む。固定は**完全一致**、PPSは範囲内(20mV刻み切り捨て)。`mA=0`は「そのプロファイルの上限まで」 |
| `requestProfile(i, mV=0, mA=0)` | プロファイルを名指し(固定が同じ電圧にあってもPPSを使いたいとき) |
| `voltage()` / `current()` | **契約値**(実測ではない) |
| `maintain()` | PPS契約の維持。`loop()`から呼ぶ。固定契約では何もしない |

`PDProfile`のフィールドは単位入りの名前です: `kind`(`PD_SUPPLY_FIXED` /
`PD_SUPPLY_PPS` / `PD_SUPPLY_BATTERY` / `PD_SUPPLY_VARIABLE`)、
`min_mv` / `max_mv`(固定は同値)、`max_ma`、`max_mw`(batteryのみ)、`raw`。

## 設計上の決めごと

- **`request()`は固定を優先します。** PPS契約は数秒ごとに再要求しないと
  死ぬ(仕様のSinkPPSPeriodicTimer、上限10秒)ので、`delay(30000)`で止まる
  sketchでも保てる固定契約を、同じ電圧が固定にあるかぎり選びます。
  PPSを使いたければ`requestProfile()`で名指しします。
- **中間電圧を勝手に丸めません。** 5/9/12V充電器に`request(8000)`は
  (PPSが無ければ)`false`です。「近いから9V」はしません。
- **batteryとvariableは列挙するだけ**で、頼む対象にしません。
- 対象は**USBPDブロックを持つ7 series**(X035/X033、L103/M103、V205、X315、
  H417、M030)。X035専用ではありません。レジスタの配置は2種類、CCのpadは
  series毎に違い、それらはvariantのdefineで供給される予定です
  (device-dataの次回取り込み後に生成。それまで手書きで進める)。

## ロジックとドライバを分けている理由

USB PDで壊れやすいのは、ビットフィールドの配置と5種類の単位換算
(10mA / 50mA / 50mV / 100mV / 20mV / 250mW)です。そこは配線が要らないのに、
実機がないと確認できない場所に置くと一番検証されません。なので
`pd_frames.c`は**レジスタもArduino.hも知らない純関数**にして、
同じ検査をhost(ctypes)と実機(rv32ec)の両方で回しています。

配置はUSB PD R3.1の仕様の値で、WCH EVTの`USBPD_SNK`と
wagiminatorの`CH32X035-USB-PD-Adapter`(CC BY-SA)を**参照のみ**で
突き合わせました。コードは持ち込んでいません。
