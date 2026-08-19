# ADR-0005: boardはfamily単位+pnumメニューとし、boards.txt/ld/variantはdevice-dataから生成する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-015, Q-011/Q-014(consumer形式の一部)。公開packager/表示名はQ-017で別途決定

## Context

対象は11 family / 27 series / 103型番。SKU追加が继续的に発生する前提で、IDEのboard選択構造とFQBN、生成方式を決める必要がある。

## Decision

**2026-08-19に改訂**(当初は「board=EVT family単位」だった。下記「改訂の理由」参照)。

1. **1 boardは1 silicon series**とする。board名は`Generic <SERIES>`(例: `Generic CH32X035`)。
   チップの刻印とboard名が一致するため、利用者はグループ名を知らなくてよい
2. 各boardの`menu.pnum`は、**先頭に`ANY`**、続いて正確な型番を並べる
3. **`ANY`はそのseriesの最小flash/最小SRAM**を宣言する。型番を知らない利用者が選んでも、
   binaryは同series全部品に収まり、stack(RAM末尾)が必ず実在メモリ内に入る
4. 型番の表示には**packageと容量を併記**する(例: `CH32X035C8T6 (LQFP48, 62K/20K)`)
5. upload backendが無いseriesはboard名へ`[compile only]`を付ける。
   現時点ではV205 / V407 / V467 / X305 / X315 / M030(probe-rsにtarget定義が無い)
6. boards.txt / linker script / vector table / variantはすべて**device-dataから生成**する。
   手編集はCIが拒否し、locked commitで検証する
7. 製品名のboard(`WeAct CH32X035 CoreBoard`等)は**別途追加する**。series boardと共存できる

## 改訂の理由: なぜfamilyではなくseriesか

当初案の`family`はWCHの**EVTリポジトリ単位**であり、利用者から見えない実装都合だった。

- チップに`CH32V307VCT6`と書いてあっても、一覧の`CH32V30x`が自分のものだと気づけない
- **`CH32V203CCT6`のfamilyは`CH32V205`**。推測不能(V205ダイをV203型番で売っている)
- 実害として、**`CH32V20x`はV203とV208でvector tableが衝突する**。slot 61がV203では
  `UART4_IRQHandler`、V208では`ETH_IRQHandler`。1 boardにまとめると`ANY`が作れない

series単位にすると、**vector tableがboardごとに一意に定まり**、24 series中20 seriesは
容量も1種類だけになるため、型番選択が実質不要になる。

`CH32V203CCT6`だけはseries=V203でありながらV205のstartupを要するため、
`CH32V205` boardへ収容する(`SKU_BOARD_OVERRIDE`)。

## Consequences

- SKU追加はdevice-dataのrecord追加+再生成で完結する(roadmap Phase 3の完了条件に一致)
- 旧コア/openwch coreとboard構造の互換はない(移行表が必要)
- 生成器はFLASH/RAM分割series(V20x/V307/V407)の扱い(独立menu軸)を今後拡張する

## Validation

- CI `generated-sync`(locked commitでの再生成一致)と`compile-matrix`(全pnum compile+size baseline)

## References

- [R-03調査](../research/board-variants-and-menus.ja.md)、[実験0003](../experiments/0003-arduino-cli-platform-poc.ja.md)、[実験0004](../experiments/0004-boards-generator-poc.ja.md)
