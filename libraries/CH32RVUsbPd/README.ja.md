# CH32RVUsbPd — PD充電器に電圧を頼む(sink)、電圧を出す(source)

[English](README.md)

旧名は `USBPD` ライブラリ（`USBPD.h`）です。定義済みのオブジェクト名 `USBPD` は変わりません。USB デバイスと合わせた使い方は
[docs/usb.ja.md](../../docs/usb.ja.md) にあります。

```cpp
#include <CH32RVUsbPd.h>

void setup() {
    if (!USBPD.begin()) {
        return;
    }
    const uint32_t start = millis();
    while (!USBPD.ready()) {
        if (millis() - start >= 5000) {
            return;                       // PD 非対応や未接続もある
        }
    }
    USBPD.request(9000);                   // 9 V。実アプリでは戻り値を確認
}
void loop() { USBPD.maintain(); }           // PPS契約の維持
```

この例は VBUS の電圧を変えるので、入力電源回路と負荷を要求電圧に対応させます。
最初の操作、CC/UART配線、成功/失敗の表示は[サンプル共通ガイド](examples/README.ja.md)を参照してください。
充電器の一覧と要求は [ListProfiles](examples/ListProfiles/README.ja.md)、ボタン操作は
[CycleVoltages](examples/CycleVoltages/README.ja.md)、Source の電源制御の骨格は
[PowerSource](examples/PowerSource/README.ja.md)にあります。

## 実装範囲

| 層 | 状態 |
|---|---|
| Source Capabilitiesの解析、プロファイル選択、Requestの組み立て | 実装済み |
| ハードウェアドライバ(CC検出・BMC送受信・GoodCRC・sinkのstate machine) | 下の系列で実装済み。固定契約とPPS契約に対応 |
| X033、M030 | `begin()`は`false`(X033はCCの端子が無い。M030は2ポートで比較器のビットが違い、未実装) |

| 系列 | CC1 / CC2 | 端子 | 状態 |
|---|---|---|---|
| CH32X035 | PC14 / PC15 | 入力 | シンクは実機確認、ソースはビルドのみ |
| CH32L103 / M103 | PB6 / PB7 | 入力 | ビルドのみ |
| CH32V205 | PA0 / PA1 | AF7 | ビルドのみ(ロット番号の5桁目が0の品にはUSBPDが無い) |
| CH32X305 / X315 | PD4 / PD5 | AF2 | ビルドのみ |
| CH32H415 / H416 / H417 | PB3 / PB4 | AF4 | ビルドのみ |

ブロックはどの系列も同じで、違うのは置き場所、CCの端子、PHYのAFIOのビットです(`usbpd_hw.h`)。
BMCのクロックはF_CPUから計算します(8 MHzのL103 / V205では送信が2.5 %速く、規格の範囲内。実機では未確認)。

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
- USBPD block を持つ series は X035、L103/M103、V205、X305/X315、H41x、M030 です(X033 は CC の端子が無い)。
  register block の配置、clock enable、IRQ、CC pad は series ごとに異なります。
  library は X035、L103/M103、V205、X305/X315、H41x の定義を持ちます。X033 と M030 では `begin()` が `false` を返します。

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

## ソース(`USBPDSource`)

同じライブラリで板を USB PD のソース(電源を出す側)にもできます(上と同じ系列)。電源の切り替えは板の仕事で、
2つのコールバックで行います。**両方とも必須**で、無ければ`begin()`が失敗します(理由は`lastErrorName()`)。
実際にその電圧が出ているかの確認はこのライブラリの範囲外です。

下記は callback の形を示す骨格です。print や `return true` だけでは電源は切り替わりません。
実際の VBUS スイッチと可変電源を制御し、要求した状態になった後に true を返す実装へ置き換えてから使います。

```cpp
bool vbus(bool on) { /* ロードスイッチ */ return true; }
bool setVoltage(uint16_t mv, uint16_t ma, bool pps) { /* レギュレータ */ return true; }

void setup() {
    USBPDSource.setRpCurrent(PD_RP_3A0);
    USBPDSource.addFixed(5000, 3000);       // 先頭は 5 V 固定
    USBPDSource.addFixed(9000, 2000);
    USBPDSource.addPps(3300, 11000, 2000);
    USBPDSource.onVbus(vbus);
    USBPDSource.onSetVoltage(setVoltage);
    USBPDSource.begin();
}
void loop() { USBPDSource.maintain(); }
```

- Rp を出してシンクの Rd を検出(tCCDebounce)、つながった CC 線を選び、`onVbus(true)` で VBUS を入れる。
- Source_Capabilities を GoodCRC が返るまで tTypeCSendSourceCap ごとに送る(nCapsCount 回で諦めると Type-C のみ:
  `attached()`だが`ready()`にならない)。
- Request は名乗ったもの(位置、電流、PPS の範囲)と照合し、外れていれば Reject。通れば Accept、tSrcTransition 後に
  `onSetVoltage()`、PS_RDY。
- コールバックが false を返したら: VBUS は入れない(取り外されるまで`VbusFailed`)、電圧は Hard Reset で 5 V に戻す
  (`VoltageFailed`)。Accept を送った後なので Reject にはできない。
- Get_Source_Cap、Soft_Reset に答え、Hard Reset からは VBUS を tSrcRecover の間落として戻す。PPS 契約は 15 秒
  Request が途絶えたら Hard Reset。実装していないものには Not_Supported(PD 3.0)/ Reject(PD 2.0)。
- 取り外し(Rd が消えた)で`onVbus(false)`。
- シンクとソースは同じ USBPD ブロックを使うので、同時にはどちらか一方(もう一方が動いていれば`Busy`)。
- ソースにする板の CC に Rd があってはいけない。WeAct の CH32X035 の板は R4 / R5(5.1 kΩ)を外す。
