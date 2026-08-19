# 実験0002: 統合startup(共通crt+vector include)の等価性検証

実施日: 2026-08-19
対象question: Q-012
関連調査: [R-01](../research/startup-files.ja.md)、実装: [prototypes/startup/](../../prototypes/startup/README.ja.md)
実施環境: WSL2 Linux x86_64、xPack riscv-none-elf-gcc 14.3.0-1([実験0001](0001-xpack-multilib-smoke.ja.md)と同一入力)。実機なし(実行は未検証)

## 目的

R-01で提案した「共通crt(`crt0_ch32.S`)+family別vector include+`-D`によるCSR初期値注入」方式が、EVTの各startupと**等価なELF**を生成することを機械検証する。

## 方法

3 familyを代表に選定(構造の異なる組合せ):

| family | 選定理由 | march/mabi | 注入define |
|---|---|---|---|
| CH32V00X | RV32E系、`.init`一体型 | rv32emc_zicsr/ilp32e | MSTATUS=0x1880, INTSYSCR=0x3 |
| CH32X035 | RV32I系、`.vector`分離型 | rv32imac_zicsr/ilp32 | MSTATUS=0x88, INTSYSCR=0x3, CORECFGR=0x1f |
| CH32V307 D8C | FPU、最大級table(103) | rv32imafc_zicsr/ilp32f | MSTATUS=0x6088, INTSYSCR=0x0b, CORECFGR=0x1f |

各familyについて「EVT startupで作ったELF」と「統合startupで作ったELF」を同一main.c(SystemInitスタブ+`SysTick_Handler`のweak上書き+無限loop)・同一系linker scriptでビルドし、以下を機械比較した(`prototypes/startup/compare.py`)。

1. **vector table照合**: 両ELFのtable(entry 0を除く全entry)が、EVT startupから抽出した仕様(`extract_vectors.py`)と一致するか。予約entryは0、IRQ entryは該当シンボルのアドレスに一致することをELFごとに検証(weak上書きした`SysTick_Handler`が両方でmain.c側を指すことも同時に確認される)
2. **CSR初期化照合**: handle_resetの逆アセンブリから(命令, CSR, 値)の集合を抽出し一致を確認。mtvec/mepcはシンボル書き込みのため「書き込みの存在」のみ確認

## 結果: 3 familyすべて合格

```
V00X:     40 entries match (evt/uni) / csr {0x804=0x3, mstatus=0x1880}
X035:     54 entries match (evt/uni) / csr {0x804=0x3, 0xbc0=0x1f, mstatus=0x88}
V307 D8C: 103 entries match (evt/uni) / csr {0x804=0xb, 0xbc0=0x1f, mstatus=0x6088}
```

サイズ(text): V00X 340(EVT)/364(統合)、X035 432/432、V307 624/624。V00Xの+24Bは`.init`一体型→`.vector`分離型への正規化に伴うsection alignment(ALIGN(64))によるもの。

観測事項:

- EVT V00X startupの`.init`末尾に仕様外の2バイト(c.nop=0x0001)が入る(EVTソース由来のアセンブラ挙動。実害なし)
- V00X系のEVT linker scriptには`.vector`出力sectionがないため、統合startupには`.vector`ルールを追加したld(2行差分)が必要
- 統合startupはCSR書き込み順をV307系(0xbc0→0xbc1→0x804→mstatus)に統一した。V00X系のEVTはmstatus→0x804の順だが、V2系のmstatus初期値(0x1880)はMIE=0のため順序差に意味はない(推測。実機確認はQ-021と併せて)

## 結論

- **「共通crt+family別vector include+define注入」方式は成立する**(Q-012の選択肢B)。vector includeの生成元をEVT .SからCSVへ替えれば、そのまま選択肢C(device-data生成)へ移行できる
- 検証ハーネス自体がそのままCIのregression testになる(GitHub Actions化はW-6)

## 再現手順

```sh
CH32_MIRROR_ROOT=/home/mt/dev_wch \
CH32_GCC_BIN=<xpack-riscv-none-elf-gcc-14.3.0-1>/bin \
prototypes/startup/run_check.sh /tmp/w2-work
# exit 0 = 全familyの等価性検証合格
```

## 残る未検証事項

- 実機でのreset動作、weak上書きISRの実発火(HILで)
- V103(j命令テーブル)、V205/M030/V407/X315/L103/H417 v3fの残りfamily(defineセットはR-01の表で確定済み、ハーネスへの追加のみ)
- H417 V5Fのloadcode boot(`CH32_LOADCODE`軸の追加が必要)
- グローバルコンストラクタ(`__init_array`)呼び出しの追加(EVT startupは呼ばないため等価性検証の対象外。own crtの新機能として別途test)
