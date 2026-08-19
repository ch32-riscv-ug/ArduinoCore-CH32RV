# ADR-0003: startup/CRT/vector/linkerはowned実装とし、共通crt0+family別vector includeで統合する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-012, Q-031(startup部分)

## Context

WCH EVTのstartupはfamily×ラインで15ファイル以上に分かれ、旧コアは固定1バリアントのincludeで上位ラインのIRQ欠落を起こしていた。EVTファイルの再配布条件も未確定(Q-030)。board/menu選択で切り替えられる保守可能な形が必要。

## Decision drivers

- family差の実体は「vector table」「CSR初期化定数」「FPU/VS」「highcode」「特殊boot」の5軸だけ([R-01](../research/startup-files.ja.md))
- vendorファイルを新repositoryへコピーしない
- グローバルコンストラクタ呼び出し(EVT startupは呼ばない)を自前で保証する
- VectorInRAM等の派生をファイル分裂なしで提供する

## Options considered

### EVT startupのpatch取込(旧コア方式)

ライン切替が固定化し、EVT再配布条件に依存する。不採用。

### 完全1ファイル(全family #if)

12 family分のtableで肥大しreview困難。不採用。

### 共通crt0+family別vector include(採用)

reset処理を単一`crt0_ch32.S`にし、CSR初期値等を`-D`注入、vector tableをinclude差し替え。13バリアントでEVT版とのELF等価性(table全entry+CSR書き込み集合)を機械検証済み(実験0002)。

## Decision

- startup/CRT/vector/linker scriptは**own実装**とし、EVTからコピーしない
- 構成は**共通`crt0_ch32.S` + family別vector include + `-D`によるCSR初期値注入**。`.init_array`(コンストラクタ)呼び出しをcrt0が行う
- vector includeは割込み番号表(事実)の転記とし、**将来`ch32-device-data`からの生成に置き換える**(IRQ表のschema追加をdata側と合意する)
- 全familyを`_vector_base`分離形式へ正規化し、VectorInRAMはlinker scriptの選択だけで提供する
- linker scriptは共通`sections.ld`+SKU別MEMORY(device-data生成)
- 等価性検証ハーネス([tests/startup/](../../tests/startup/README.ja.md))をCIで常時実行し、EVT更新起因の差分を検出する

## 実機で判明した制約(2026-08-20追記)

**ベクタテーブルはFLASHの先頭に置き、entry 0をリセットジャンプそのものにする。**

QingKe V2(CH32V003/V00x)の`mtvec`はベースアドレスの下位ビットを捨てる。
テーブルを`.init`の後ろ(アドレス8)へ置くと`mtvec`は`0x03`と読め、
全割込みがアドレス0へ飛ぶ。CH32V003実機で計測
([実験0011](../experiments/0011-milestone1-serial-on-v003.ja.md))。

EVTのV003 startupが同じlayoutを採っているのはこの制約のためである。
ベース0はどの世代でもalignedなので、**全familyでこの1つのlayoutに統一する**。
`sections.ld`が`ASSERT(_vector_base == 0, ...)`で、
`tests/startup/compare.py`が13 variantすべてで同じことを検証する。

等価性harnessは当初**テーブルの中身しか比較していなかった**。
「中身が正しい」ことと「そのテーブルが実際に使われる」ことは別であり、
配置アドレスの検証を後から追加した。

## Consequences

- EVT再配布条件(Q-030)がstartupに関して無関係になる(vendor取込の最小集合はデバイスheader側の論点に縮小)
- CH32V103(j命令テーブル)とCH32H417(loadcode boot)は追加対応が必要(現状ハーネス対象外)
- mstatus/INTSYSCR等の初期値はEVT踏襲。HPE/nesting設定の見直しはQ-021の実測後

## Validation

- CI `startup-equivalence`: 13バリアントのELF等価性
- compile-matrixの`.init_array`静的検査。コンストラクタ実行と割込み実発火はHILで確認する(未了)

## References

- [R-01調査](../research/startup-files.ja.md)、[実験0002](../experiments/0002-unified-startup-poc.ja.md)、[実験0008](../experiments/0008-lto-and-interrupt-attr.ja.md)
