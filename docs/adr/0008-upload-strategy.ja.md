# ADR-0008: 書き込みのdefaultはWCH-LinkEとし、経路カバレッジを段階的に増やす

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-040, Q-044, Q-045(いずれも実機認定・実装は継続)

## Context

書き込み経路はfamilyによって大きく異なる(debug IF: SDI 1-wire/SWD 2-wire、工場ブートローダのUSB/UART ISP有無、ソフトエントリ可否、M030はISPなし)。互換書き込み器も多数あり、書き込みソフトも複数ある。defaultと拡張順を決める必要がある。調査は[R-17](../research/upload-programmers.ja.md)。

## Decision drivers

- 全family共通・ブランクチップ可・verify/reset完結の経路をdefaultにする
- 開発・fixture・HILと同一経路で認定コストを一本化する
- 「工場BLへのソフトエントリ可否」でfamilyが2系統に割れる事実(自動書き込みの可否)
- probe-rs 0.32のtarget gap(V407/X315/M030/V205)

## Options considered

### USB-ISPをdefault

ハード不要だが、V003/V00X(USBなし)とM030(BLなし)をカバーできず、BOOT0ピン系は自動entry不可。オプション扱いとする。

### WCH-LinkEをdefault(採用)

全family対応、$4〜7、UART内蔵でSerialモニタも同一装置。無印WCH-LinkはV003/V00X系を書けないため、案内はLinkE基準にする。

## Decision

- **default書き込みはWCH-LinkE(debug IF経由)**。開発・fixture・HILもWCH-LinkEで進める
- backendはprobe-rsを第一候補として維持し、target gap(V407/X315/M030/V205)はwlink/WCH OpenOCDの併用またはupstream貢献で埋める。**backend差はfrontend(`ch32-upload`案、[upload-and-fixture](../upload-and-fixture.ja.md))で吸収し、メニューには経路名だけを見せる**
- **カバレッジは段階的に追加**する(upload_methodメニュー、family別に出し分け):
  1. WCH-Link(default、全family)
  2. USB-ISP(wchisp): USB持ちfamily。X03x/X315/H417はCDC 1200bps touch+`SystemReset_StartMode()`による自動書き込みを実装候補、BOOT0ピン系は手動BOOT注記付き
  3. UART-ISP: V003/V00Xの工場UART BL
  4. board固有ブートローダ: named board単位(UIAPduino=rv003usb HID等)。pnum項目へ`upload.tool`を紐付ける
- 互換書き込み器は認定Tierで管理する: **Tier1=WCH-LinkE(+USB ISP)**、Tier2=LinkW/無印Link(2-wire系限定)/minichlink系probe(ESP32-S2、Ardulink、NHC-Link042、rvswdio)/rv003usb BL、Tier3=実験的(picorvd、ESP32-S3独自実装、Flipper等)。Tier2以下は「動作報告歓迎・未認定」と明示する
- 独自ブートローダ・書き込みソフトの自作は排除しない(既定方針どおり)。工場BL+ソフトエントリで足りる範囲を先に使い、独自BLはboardプロダクト向け付加価値として設計する

## Consequences

- 利用者への最小案内が「WCH-LinkEを1本用意」で全SKUに通る
- probe-rs gapのfamilyは当面backendが分かれる(frontendで隠蔽。認定matrixはbackend別に記録)
- wchisp(GPL-2.0)のtool配布はソース入手先明記で対応(openwch式の手動配置は採らない)
- M030はWCH-Link一択である旨をboard文書に明示する

## Validation

- Q-040系の実機認定matrix(probe-rs/wlink×family×LinkE FW)
- 自動書き込み(1200bps touch+SWエントリ)の実機確認(X035から)
- Tier2 programmerは動作報告ベースで随時昇格を判定

## References

- [R-17調査](../research/upload-programmers.ja.md)(対応表、programmer一覧、ソフト比較、URL付き)

---

## 追記(2026-09-16): 同梱uploaderをprobe-rsからch32rvへ置換

本ADRの決定時点では`platform.txt`のupload recipeはprobe-rsだった。2026-09-01の
[ch32rv-requests](../ch32rv-requests.ja.md)の前提で「**probe-rsを同梱した状態では
リリースしない。コア同梱のアップローダはch32rvに一本化する**」と決まり、ch32rv 0.8.0
(tag `v0.8.0`、commit `12e88e5`)で依頼A・Bが出揃ったため、実際に置き換えた。

### 変わったもの

| | 置換前 | 置換後 |
|---|---|---|
| recipe | `probe-rs download --chip <SKU> --reset` | `ch32rv flash <elf> --format elf --chip <name> --reset run --confirm-run` |
| tool | `tools.probe_rs` | `tools.ch32rv` |
| programmer | `wch-link` → `probe_rs` | `wch-link` → `ch32rv`(**idは据え置き**) |
| chip欄 | `build.probe_rs_chip` | `build.ch32rv_chip`(**probe_rs_chipも残す**) |
| 語彙の源泉 | `tools/index/probe_rs_targets.csv` | `tools/index/ch32rv_chips.csv`(ch32rv埋め込みDB=`ch32-device-data@e3e723a`由来) |
| package index | `tools_probe_rs.json` | `tools_ch32rv.json` |

**programmer idを`wch-link`のまま据え置いた**のは、idが道具ではなく**プローブ**を指す
名前であり、生成済みの39個の`sketch.yaml`がそのまま使えるため。

**`build.probe_rs_chip`を残した**のは、ベンチのharnessが検出チップ→boardの解決に
この欄を使っているため(`tests/manual/smoke.boards_for`)。harness側のch32rv移行は
別作業であり、同梱物の話とは分けている。

### `--chip`値の決め方が変わった

probe-rsはfamily名を受け付けないので、menu entryごとに具体的な型番を当てる必要が
あった。ch32rvは**family名も受ける**ので2段になった(`ch32rv_chip()`):

1. ch32rvが知っている型番ならそれ。**別familyのチップが挿さっていれば`target-ambiguous`
   (exit 23)で弾かれる**ので、ボード取り違えを捕まえられる
2. 知らない型番ならfamily名。ch32rvがattach時にchip_idから実物を解決するので、
   **flash geometryが我々の推測ではなくシリコン由来になる**

probe-rs版にあった「同系列の別の型番で代用する」段は**意図的に落とした**。probe-rsは
flash *algorithm*を選ぶだけだったが、ch32rvはFLASH controllerを直接叩き、page消去粒度を
名指しした型番から取る。`CH32V006F4U6`は16Kで最も近い既知の兄弟`CH32V006E8R6`は62Kなので、
サイズ違いの代用は実害になりうる。

### fail-closed

ch32rvのDBに無いseriesは`build.ch32rv_chip`を出さない。uploadを試みると
ch32rvの`target-not-in-db`(exit 20)で止まり、**挿さっている別のチップに書き込むことはない**。
0.8.0時点でこれに当たるのは未発売の7 series(V205/V407/V467/X305/X315/M030/M103)で、
`[compile only]`と表示されるboardと完全に一致する。これは依頼B-3の受け入れ基準そのもの。

機構としては、arduino-cliが未定義プロパティを**プレースホルダのまま**渡し、ch32rvが
その名前を知らないものとして弾く、という形になる(実測、V205のFQBNでV006を挿した状態):

```
--chip {build.ch32rv_chip} is not in the target DB (detected CH32V006 from chip_id 0x00600620)
  hint: omit --chip to use auto-detection, or run `ch32rv db list` for the names this build knows
Failed programming: uploading error: exit status 20
```

**この安全性はch32rv 0.8.0の`target-not-in-db`に依存している。** 0.7.0以前は未知の
`--chip`値を「照合できない」として受理していた(`cli/src/session.rs`)ので、同じ操作が
黙って挿さっているV006に書き込んでいた。`--chip ""`(空文字)は0.8.0でも自動検出に
フォールバックして書き込むので、**空値ではなくプレースホルダが残ることが効いている**。
recipeから`--chip`を落としたり空にしたりする変更は、このfail-closedを壊す。

### 実機検証(2026-09-16、CH32V006K8U6)

```
arduino-cli upload --programmer wch-link \
  --fqbn ch32-riscv-ug:ch32v:CH32V006:pnum=CH32V006K8U6
→ flashed 3140 bytes to CH32V00X / verify: OK (readback matches) / running: yes
→ VCP: serial_println READY / PING→PONG / RUN 4行完走
```

`running: yes`は`--confirm-run`の出力で、**書けたこと**ではなく**走っていること**を
確認している。probe-rs recipeには無かった保証。

### windows arm64について

依頼B-1の「6 platform binary」は**バイナリ6種ではなくpackage_index.jsonのhostスロット
6個**の意味だった。既存の`tools_probe_rs.json`が`i686-mingw32`にx64アーカイブを
そのまま割り当てていたのがその証拠である。`tools_ch32rv.json`は同じ方式で
**5つの成果物から7つのhostスロット**を作っており、`arm64-mingw32`にもx64の`.zip`を
指している(Windows on ARMはx64をエミュレーション実行し、USBも通常のWin32 API経由)。
native arm64ビルドは要望が出てから検討でよい。
