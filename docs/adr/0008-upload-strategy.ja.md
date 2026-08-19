# ADR-0008: 書き込みのdefaultはWCH-LinkEとし、経路カバレッジを段階的に増やす

- Status: Accepted
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
