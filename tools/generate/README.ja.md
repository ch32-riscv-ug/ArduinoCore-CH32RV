# W-4 prototype: device-data → boards.txt/ld generator

状態: proof of concept(2026-08-19)。リリース対象ではありません。
関連: [環境整備計画](../../docs/infrastructure.ja.md) W-4、[R-03](../../docs/research/board-variants-and-menus.ja.md)、[実験0004](../../docs/experiments/0004-boards-generator-poc.ja.md)

## 目的

`ch32-device-data`のcatalog/・evidence/・index/(正規化CSV)から、prototype platformの`boards.txt`とSKU別linker scriptを機械生成する。R-03の設計(family単位board+pnumメニューに全型番、生成物はcommitしてCIで再生成一致を検証)の最小実装。

## 使い方

```sh
# 生成(生成物はcommit対象)
python3 generate.py --tables /path/to/ch32-device-data \
                    --platform ../platform/ch32v

# CI用: commit済み生成物と再生成結果の一致検証(drift検出でexit 1)
python3 generate.py --tables ... --platform ... --check

# レビュー用: driftしたファイルのunified diffも出す(--checkを含む)
python3 generate.py --tables ... --platform ... --diff
```

`--check`はdriftがあると末尾に**採用サマリ**を出します。

```
adoption summary: 1 additive, 0 rewriting existing lines
```

`additive`は行が増えただけ(新しいroute・pad・型番)、`rewriting`は既存の行が
変わったか消えたもの(pin番号・既定route・board名・メモリサイズが動きうる)です。
**後者だけが取り込み前に理解を要します。** lock自身は毎回動くので数えません。
この判別が成立するのは生成物ヘッダにcommit idを入れていないためで、
入れていた頃は毎回全ファイルが1行失っていて信号が埋もれていました。

## device-dataの取り込み手順

**取り込みはリリース準備の最初の工程で、手動でのみ行います**(自動化しない)。
取り込みだけ先に進めるとリリース物とずれるためです。

```sh
# 1. 上流を取得して見るだけ(ツリーは書き換わらない)
git -C .tools/ch32-device-data fetch origin
git -C .tools/ch32-device-data checkout origin/main
uv run --no-project python tools/generate/generate.py \
    --tables .tools/ch32-device-data --platform . --check

#    exit 0 なら生成物は変わらない。取り込む必要がない
#    → 手順4でcloneを戻して終わり

# 2. 差分を見る
uv run --no-project python tools/generate/generate.py \
    --tables .tools/ch32-device-data --platform . --diff

# 3. 取り込む(lockのcommitとhashもここで動く)
uv run --no-project python tools/generate/generate.py \
    --tables .tools/ch32-device-data --platform .
cd tests && uv run pytest -q generated compile/test_compile_matrix.py sizebench

# 4. 取り込まないと決めたら、cloneをlockedへ戻す
uv run --no-project python tools/index/fetch_tools.py --tool ch32-device-data
```

手順4を忘れると、`.tools`のcloneがlockと違うcommitのまま残り、
ローカルの`generated-sync`が偽の失敗を出します。

`rewriting`が出た場合は、該当seriesの実機があるなら取り込み後に一度動かします。
pin番号や既定routeが変わると、既存のsketchの意味が変わるためです。

## 生成規則

- boardは家族単位(`FAMILY_CONFIG`)。pnumメニューに該当familyの**全型番**を列挙
- 並び順は`series_order`(V002→V004→…→M007)+型番昇順で決定的。FQBNの既定(先頭項目)が再生成で変わらない
- pnum項目が`build.board`/`build.series`(vendorデバイス選択マクロ。V007/M007→`CH32V007_M007`)/`build.ldscript`/`upload.maximum_*`を注入
- ldはユニークな(FLASH, SRAM)組合せごとに1本(`ch32v00x_62k_8k.ld`等)。MEMORYのみ持ち、共通`sections.ld`をINCLUDE
- 生成物ヘッダに`DO NOT EDIT`を記録(タイムスタンプは入れず再生成をidempotentに保つ)
- vector tableは`build.vector_variant`という**1つのstem**で選ぶ。platform.txtが`vectors_<stem>.inc`/`irqn_<stem>.h`/`exti_<stem>.h`の3つを組み立てるので、**pnum項目が1行上書きするだけでdie variantを差し替えられる**(CH32V203RBT6。どのpartがどのmacroかは`evt_variants.csv`由来で手書きしない)。`ANY`はboardの既定を保つ——既にseries最小のflashを宣言している「特定の石向けではない」項目なので
- **source tablesのgit commitは`vendor/ch32-device-data.lock.toml`に1か所だけ**置く。生成物ヘッダにcommitを入れていた頃は、中身が変わらないupstream bumpでも全生成物が更新されていた。lockはgeneratorの出力の1つなので`--check`がそのまま検証する。lockは`read_table()`が実際に読んだ表のSHA-256も持ち、**この5表に触れないupstream commitは生成物を変えられない**ことを表す

## 現在の範囲と今後

- 対象はCH32V006 family(26 SKU)のみ。他familyは`FAMILY_CONFIG`への追加+等価性検証済みのstartup defines(実験0002)で拡張する
- march/mabi・CSR初期値は当面generator内の設定表。将来はcores.csv/series.csvやdevice-data側への移管を検討(判断ポイント)
- variant(pins_arduino.h)の生成は未実装。pins.csv/pin_functions.csvからのpin map生成はArduinoピン設計の合意後
- package index生成(W-5)は未実装

## 全family展開の計画(2026-08-19調査)

「自動生成できるものは全部作る」方針での棚卸し。

### 対象family

等価性ハーネス([tests/startup/](../../tests/startup/README.ja.md))が実証済みの**10 family**が対象。
除外は`CH32V103`(vector tableがj命令形式)と`CH32H417`(loadcode boot)で、これはハーネス側の除外理由と同じ。

| family | series | SKU | march / mabi | vector variant |
|---|---|---:|---|---|
| CH32V003 | V003 | 4 | rv32ec_zicsr / ilp32e | v003 |
| CH32V006 | V002/V004/V005/V006/V007/M007 | 26 | rv32emc_zicsr / ilp32e | v00x(生成済み) |
| CH32V205 | V205 | 3 | rv32imc_zicsr / ilp32 | v205 |
| CH32V20x | V203 / V208 | 17 | rv32imac_zicsr / ilp32 | **series別**: V203→d6、V208→d8w。ただし**CH32V203RBT6だけd8**で、pnum項目が`build.vector_variant`を上書きする |
| CH32V307 | V303/V305/V307/V317 | 14 | rv32imafc_zicsr / ilp32f | **series別**: V303→d8、V305/V307/V317→d8c(`evt_variants.csv`と`ch32v30x.h`のコメントに一致) |
| CH32V407 | V407/V467 | 6 | rv32imac_zicsr / ilp32 | v4x7 |
| CH32X035 | X033/X035 | 8 | rv32imac_zicsr / ilp32 | x035 |
| CH32X315 | X305/X315 | 4 | rv32imafc_zicsr / ilp32f | x3x5 |
| CH32L103 | L103/M103 | 7 | rv32imac_zicsr / ilp32 | l103 |
| CH32M030 | M030 | 5 | rv32imc_zicsr / ilp32 | m030 |

CSR初期値(`CH32_MSTATUS_INIT`/`CH32_INTSYSCR_INIT`/`CH32_CORECFGR`/`CH32_CSR_BC1`/`CH32_CSR805_CLR`)は
**family単位で確定**しており、family内で分かれるのはvector tableだけ。
V20xのD6/D8/D8WもV307のD8/D8Cも、CSRとmarch/mabiは同一。

### すぐ自動化できるもの

- board entry、pnum menu、flash/sram、`upload.maximum_size`、ldscript選択 → device-data `products.csv`から
- linker script(MEMORY) → 同上
- march / mabi / CSR初期値 → 等価性ハーネスの表を`FAMILY_CONFIG`へ移すだけ

ハーネス([tests/startup/startup_equivalence.py](../../tests/startup/startup_equivalence.py))と`FAMILY_CONFIG`が
同じ値を二重に持つことになるため、**片方を正本にしてもう片方が参照する**か、
CIで一致を検証するtestを追加する。

### ブロッカー1: vector table 13本

`-DCH32_VECTORS=vectors_<x>.inc`が指すファイルが無いとcompileできない。現在あるのは`vectors_ch32v00x.inc`のみ。

必要なのは**13本**(38〜103 entry、合計約900 entry)。family内で統合できるものはない
(V20x: D6/D8間7行差、D8/D8W間4行差。V307: D8/D8C間はUSBWakeUp等で差)。

`tests/startup/extract_vectors.py`でEVT startupから機械的に抽出できることは確認済み
(13本すべて成功、j命令形式ゼロ)。ただし既存の`vectors_ch32v00x.inc`は
「Reference Manualの割込み番号表の自前転記、EVTはCIでの照合相手」という位置づけであり、
EVT抽出結果をそのままcommitするかは**方針判断が必要**([vendor-policy](../../docs/vendor-policy.ja.md))。

長期的な正解はhandoffに書かれている「device-data生成」。`ch32-device-data`に割込みtableが
存在しないため、まずupstreamへtable追加が要る。

### ブロッカー2: f_cpu(クロック方針が未整理)

#### 現状: 3層のうちどれも埋まっていない

- `crt0_ch32.S:145`が`jal SystemInit`を呼ぶ**フックはある**
- しかし`SystemInit()`は`cores/arduino/wiring_stub.c`で**空実装**
- `boards.txt`の`F_CPU=48000000L`は`-DF_CPU=`で渡っているが、**コア側に読んでいる箇所がゼロ**

つまり実チップはリセット既定で動き、48MHzは宣言だけの値。
**`F_CPU`は実際のSYSCLKと一致していなければならない**(delay、baud、PWM周期がすべてこれを使う)ため、
現状は不整合。

#### 整理すべき3層

| 層 | 内容 | 置き場所 |
|---|---|---|
| 1. チップ事実 | HSI周波数、HSEの有無、PLL、最大SYSCLK | family(生成器の`FAMILY_CONFIG`) |
| 2. ボード事実 | 水晶が実装されているか、何MHzか | **board / variant** |
| 3. コアの決定 | `SystemInit()`が設定するSYSCLK。`F_CPU`はその結果を宣言する | family(HSI固定なら)/ board(HSE使用なら) |

EVT headerの`HSE_VALUE`は**EVT開発ボードに載っている水晶の想定値**であって、チップの属性ではない。
Arduinoが`f_cpu`をboards.txtに置くのはこのため。

#### チップ事実(EVT device headerから実測、2026-08-19)

| family | HSI | HSE_VALUE(EVT想定) |
|---|---:|---:|
| CH32V003 | 24 MHz | 24 MHz |
| CH32V006 | 24 MHz | 24 MHz |
| CH32V205 | 8 MHz | 8 MHz |
| CH32V20x | 8 MHz | 32 MHz |
| CH32V307 | 8 MHz | 8 MHz |
| CH32V407 | 20 MHz | 25 MHz |
| **CH32X035** | **48 MHz** | **定義なし(HSEを持たない)** |
| CH32X315 | 20 MHz | 20 MHz |
| CH32L103 | 8 MHz | 8 MHz |
| CH32M030 | 8 MHz | 8 MHz |

#### CH32X035が最も単純

EVTの`system_ch32x035.c`が提供する選択肢は`8 / 12 / 16 / 24 / 48 MHz`で、**すべてHSIの分周**。
PLLもHSEも使わない。EVT既定は`SYSCLK_FREQ_48MHz_HSI = HSI_VALUE` = 48MHz。

→ X035は`F_CPU=48000000L`とし、`SystemInit()`は分周器を`/1`にするだけでよい。
水晶が無いため個体差・実装差が入らず、初期HILの対象として扱いやすい。

他familyはPLLが要る(V003/V006はHSI 24MHz→48MHzならPLL×2、V20x/V307/L103/M030はHSI 8MHzから逓倍)。

#### 未決定

- familyごとの既定SYSCLK(最大を狙うか安全側にするか)
- **HSEを使うか**。使うとf_cpuがboard属性になり、variantに水晶情報が要る。
  初期は**全familyでHSI固定**が現実的(ボード差を持ち込まない)
- クロックをメニュー化するか(STM32duino等は持つ)。ADR-0007の`build.extra_flags`方針とは別軸
