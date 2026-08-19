# ADR-0005: boardはfamily単位+pnumメニューとし、boards.txt/ld/variantはdevice-dataから生成する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-015, Q-011/Q-014(consumer形式の一部)。公開packager/表示名はQ-017で別途決定

## Context

対象は11 family / 27 series / 103型番。SKU追加が继续的に発生する前提で、IDEのboard選択構造とFQBN、生成方式を決める必要がある。

## Decision drivers

- 利用者はチップ刻印の型番(family名込み)で探す
- board一覧の肥大はesp32型の実害(メニュー崩壊、手編集PR集中)がある
- STM32duinoのpnum方式は1 boardあたり153項目まで実運用実績がある
- exact SKUを正本にboards.txtを生成する方針([R-03](../research/board-variants-and-menus.ja.md))
- CH32はWCH-Link書込みのためUSB VID/PIDによるboard自動検出が効かず、board細分化に検出上の利点がない

## Options considered

### SKUごと個別board(103 board)

一覧が破綻方向。SKU追加=board増殖。不採用。

### series単位board(27)

series名は型番から自明でない。不採用。

### family単位board+pnumメニュー(採用)

一覧11行、最大メニュー26項目。26/26 SKUのcompile matrixで実証済み(実験0004)。

## Decision

- boardは**family単位**(例: `CH32V00X`)とし、`menu.pnum`に該当familyの**全型番**を列挙する。温度グレード違い等ビルド同一のSKUも項目としては全部出し、variant/ldの実体を共有する
- **boards.txt・SKU別linker script・variantは`ch32-device-data`から自動生成**し、生成物をcommitする。手編集はCIが拒否(esp8266方式)。生成物ヘッダにdata側commitを記録し、CIがそのcommitで再生成一致を検証する(lock機構)
- pnum項目の順序は決定的(series正順+型番昇順)とし、FQBNの既定(先頭項目)が再生成で変わらないことを保証する。CI・文書のFQBNは常に`:pnum=`付きで書く
- 開発用の暫定IDは**packager=`ch32-riscv-ug`、architecture=`ch32v`**(FQBN例: `ch32-riscv-ug:ch32v:CH32V00X:pnum=CH32V006K8U7`)。公開時のIDと表示名はQ-017で確定する(変更時は生成器の設定1箇所)
- 市販ボードは独立boardにせずpnum項目として追加し、需要が確認できたものだけ独立board化する

## Consequences

- SKU追加はdevice-dataのrecord追加+再生成で完結する(roadmap Phase 3の完了条件に一致)
- 旧コア/openwch coreとboard構造の互換はない(移行表が必要)
- 生成器はFLASH/RAM分割series(V20x/V307/V407)の扱い(独立menu軸)を今後拡張する

## Validation

- CI `generated-sync`(locked commitでの再生成一致)と`compile-matrix`(全pnum compile+size baseline)

## References

- [R-03調査](../research/board-variants-and-menus.ja.md)、[実験0003](../experiments/0003-arduino-cli-platform-poc.ja.md)、[実験0004](../experiments/0004-boards-generator-poc.ja.md)
