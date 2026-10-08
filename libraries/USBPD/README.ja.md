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

## 実装範囲

| 層 | 状態 |
|---|---|
| Source Capabilitiesの解析、プロファイル選択、Requestの組み立て | 実装済み |
| ハードウェアドライバ(CC検出・BMC送受信・GoodCRC・sinkのstate machine) | CH32X035/X033で実装済み。固定契約とPPS契約に対応 |
| L103/M103/V205/X315/H417/M030 | registerとCC padの定義が無いため`begin()`は`false` |

ドライバが自分でやること(USB PD R3.1に沿う):

- **どの呼び出しでも状態が進む。** `ready()`だけを待つループで契約まで行きます(接続検出・仕様のタイマ・再送・PPS維持は`ready()` / `connected()` / `request()` / `maintain()`のどれからでも回る)。
- Source_CapabilitiesにはRequestで答える(仕様上の義務)。sketchが最後に得たプロファイルと**同じPDO(生の値まで一致)**が同じ番号にあればそれ、それ以外(別の充電器、表が変わった、不明)はprofile 0(5 V)。取り外しを検出したら選択は忘れる。
- **契約が残ったまま起動した場合**(リセット、書き込み直後): Source_Capabilitiesが来ないので、tTypeCSinkWaitCap(620 ms)後にSoft_Reset(VBUSは落ちない)、駄目ならHard Reset(VBUSが一旦落ちて5 Vで戻る。VBUSだけで動く板は再起動する)を最大2回。応答しない充電器はType-Cのみ: `connected()`だが`ready()`にならない。
- GoodCRCが返らないメッセージは2回再送、重複受信は無視、応答タイマ(Accept 30 ms、PS_RDY 550 ms)切れはSoft/Hard Reset。
- Get_Sink_Capには5 VのSink_Capabilitiesで、実装していないものにはNot_Supported(PD 3.0)/ Reject(PD 2.0)で答える。
- **PPS契約は5秒ごとに再要求**(充電器は10秒黙ると契約を落とす)。`maintain()`を呼んでいる間だけ。
- CCがvRd-Connectを下回り続けたら取り外しとみなす(同じVBUSで給電されていない板でのみ意味がある)。

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
| `voltage()` / `current()` | **契約値**(実測ではない)。Requestの刻みで返る(PPSは20 mV / 50 mA切り捨て) |
| `contractProfile()` | 契約中のプロファイルの番号。契約なしは`-1` |
| `pps()` | 契約がPPS(=`maintain()`が要る) |
| `maintain()` | ドライバの心拍(接続・取り外し、タイマ、再送、PPS維持)。`loop()`から呼ぶ |

`PDProfile`のフィールドは単位入りの名前です: `kind`(`PD_SUPPLY_FIXED` /
`PD_SUPPLY_PPS` / `PD_SUPPLY_BATTERY` / `PD_SUPPLY_VARIABLE`)、
`min_mv` / `max_mv`(固定は同値)、`max_ma`、`max_mw`(batteryのみ)、`raw`。

## 設計上の決めごと

- **`request()`は固定を優先します。** PPS契約は数秒ごとに再要求しないと
  死ぬ(仕様のtPPSTimeout、10秒)ので、`delay(30000)`で止まる
  sketchでも保てる固定契約を、同じ電圧が固定にあるかぎり選びます。
  PPSを使いたければ`requestProfile()`で名指しします。
- **中間電圧を勝手に丸めません。** 5/9/12V充電器に`request(8000)`は
  (PPSが無ければ)`false`です。「近いから9V」はしません。
- **batteryとvariableは列挙するだけ**で、頼む対象にしません。
- USBPD block を持つ series は X035/X033、L103/M103、V205、X315、H417、M030 です。
  register block の配置、clock enable、IRQ、CC pad は series ごとに異なります。
  library が hardware 定義を持つのは X035/X033 だけで、他の series では `begin()` が `false` を返します。

## ロジックとドライバを分けている理由

USB PDで壊れやすいのは、ビットフィールドの配置と単位換算
(10mA / 50mA / 50mV / 100mV / 20mV / 250mW)です。そこは配線が要らないのに、
実機がないと確認できない場所に置くと一番検証されません。なので
`pd_frames.c`は**レジスタもArduino.hも知らない純関数**にして、
同じ検査をhost(ctypes)と実機(rv32ec)の両方で回しています。

配置はUSB PD R3.1の仕様の値で、WCH EVTの`USBPD_SNK`と
wagiminatorの`CH32X035-USB-PD-Adapter`(CC BY-SA)を**参照のみ**で
突き合わせました。コードは持ち込んでいません。

## 基板の注意

CC線は短く。WeActの板でCCからprobeのピンへ長いリードを引いたところ、
負荷でメッセージが1つもdecodeできなくなりました。
