# R-03: SKUバリエーションとboard/menu構造

調査基準日: 2026-08-19
関連: [Q-001, Q-015, Q-017](../open-questions.ja.md)、[ADR-0001](../adr/0001-device-data-repository.ja.md)

## 調査目的

対象は全CH32ファミリ(CH32V00Xの26バリエーションは代表例)。以下を明らかにする。

1. SKUバリエーションの実体(数、差分軸、例外)
2. Arduino IDEのボード選択とカスタムメニューをどう構成するか(他コアの前例と実害)
3. `ch32-device-data`からの自動生成に載る構造か

## 確認済み事実

### 1. 全体規模

`ch32-device-data/tables/`(families/series/products/packages/pins/pin_functions等の正規化CSV)で確認。

- **11 family(ミラーrepository単位)/ 27 series(die単位)/ 103注文型番**
- family別型番数: CH32V006系=26、CH32V20x=17、CH32V307=14、CH32X035=8、CH32L103=7、CH32V407=6、CH32M030=5、CH32H417=5、CH32V003=4、CH32V103=4、CH32V205=4、CH32X315=4
- core/ISAは13種(RV32EC〜RV32IMABCF、V407系はvector拡張Zve64x付き、H417はdual core)。→ toolchain要件は[R-04](toolchain-distributions.ja.md)

### 2. CH32V00Xの「26バリエーション」の検証

英語版datasheet 4冊(`CH32V002DS0` `CH32V004DS0` `CH32V006DS0` `CH32V007DS0`、各物理p.2の製品比較表とChapter 4)から全SKUを抽出した結果、**26 = V002:5 + V004:2 + V005:4 + V006:7 + V007:4 + M007:4**でちょうど一致する。

| 型番 | パッケージ | Flash | RAM | GPIO | 温度上限 | 特記 |
|---|---|---|---|---|---|---|
| CH32V002F4P6 | TSSOP20 | 16K | 4K | 18 | 85℃ | |
| CH32V002F4U6 | QFN20 | 16K | 4K | 18 | 85℃ | |
| CH32V002A4M6 | SOP16 | 16K | 4K | 14 | 85℃ | |
| CH32V002D4U6 | QFN12 | 16K | 4K | 11 | 85℃ | SPIなし |
| CH32V002J4M6 | SOP8 | 16K | 4K | 6 | 85℃ | SPIなし |
| CH32V004F6P1 | TSSOP20 | 32K | 6K | 18 | **0〜70℃** | 民生グレードのみ |
| CH32V004F6U1 | QFN20 | 32K | 6K | 18 | **0〜70℃** | 〃 |
| CH32V005E6R6 | QSOP24 | 32K | 6K | 22 | 85℃ | OPA/TouchKeyなし |
| CH32V005F6U6 | QFN20 | 32K | 6K | 18 | 85℃ | 〃 |
| CH32V005F6P6 | TSSOP20 | 32K | 6K | 18 | 85℃ | 〃 |
| CH32V005D6U6 | QFN12 | 32K | 6K | 11 | 85℃ | 〃、SPIなし |
| CH32V006K8U7 | QFN32 | 62K | 8K | 31 | 105℃ | |
| CH32V006E8R6 | QSOP24 | 62K | 8K | 22 | 85℃ | |
| CH32V006E8R7 | QSOP24 | 62K | 8K | 22 | 105℃ | |
| CH32V006F8U7 | QFN20 | 62K | 8K | 18 | 105℃ | |
| CH32V006F8P7 | TSSOP20 | 62K | 8K | 18 | 105℃ | |
| CH32V006F4U6 | QFN20 | **16K** | **4K** | 18 | 85℃ | **V006名だがV002相当の廉価版(OPAなし)** |
| CH32V006D8U7 | QFN12 | 62K | 8K | 11 | 105℃ | SPIなし、ADC 4ch |
| CH32V007E8R6 | QSOP24 | 62K | 8K | 22 | 85℃ | OPA+CMP×2 |
| CH32V007E8R7 | QSOP24 | 62K | 8K | 22 | 105℃ | 〃 |
| CH32V007K8U6 | QFN32 | 62K | 8K | 31 | 85℃ | 〃 |
| CH32V007K8U7 | QFN32 | 62K | 8K | 31 | 105℃ | 〃 |
| CH32M007E8R6 | QSOP24 | 62K | 8K | 15 | 85℃ | 24V P+Nプリドライバ内蔵 |
| CH32M007E8U7 | **QFN26C3** | 62K | 8K | 16 | 105℃ | 〃(ピンコードEだが26ピン) |
| CH32M007G8R6 | QSOP28 | 62K | 8K | 12 | 85℃ | 48V N+Nプリドライバ |
| CH32M007K8U7 | QFN32 | 62K | 8K | 17 | 105℃ | 〃 |

- 中国語版datasheetのみに**CH32M006A8U7**(QFN16、65K/8K、5V×1Aフルブリッジ内蔵)が存在し、含めると27になる。英語版未発行のため「26」は英語版基準の数字(推測)。将来の27個目として予約しておくのが安全
- datasheet注記: 温度グレード違い(末尾6と7)は「温度範囲以外の仕様は同一」→ **ビルド成果物は同一にできる**
- 型番命名規則(datasheet「Series Product Naming Rules」): `CH32 V 006 K 8 U 7` = ファミリ文字(V=汎用RISC-V/M=モータ/L=低電力/X=特殊周辺) + 系列番号 + ピン数文字(J=8/D=12/A=16/F=20/E=24/G=28/K=32/…/Z=144) + Flashコード(4=16K/6=32K/8=64K/B=128K/C=256K) + パッケージ文字(T=LQFP/U=QFN/R=QSOP/P=TSSOP/M=SOP) + 温度コード(1=0〜70/6=-40〜85/7=-40〜105/3=-40〜125)
- 注意: Flashコード8は名目64Kだが比較表の実容量は**62K**(=63488B)。`upload.maximum_size`にはこの実容量を使う

### 3. ch32-device-dataの対応状況

- `candidates/`に自動抽出レコード103件(V00X系26件すべて含む)。パッケージ・Flash/RAM・温度・pin表・provenance付き
- `devices/`(正式昇格済み)は8件のみ。V00X系はCH32V006K8U7のみ
- `tables/`のCSVはfamily→series→products→pins/pin_functionsの階層+packages/coresマスタで、**boards.txt/variant生成の入力に十分な構造**(提案: 生成はこのrepositoryの固定releaseを入力にする)

### 4. 既存CH32系コアのboard構造

- ローカル旧コア(`arduino_core_ch32_riscv_noneos`): **7ボード(series単位)、SKU表現なし**。`menu.memory`(Flash/SRAM組合せ)は`upload.maximum_size`等の表示にしか効かず、**ldは固定Link.ldのため実サイズと乖離しうる**。variant機構は未使用。march/mabiはボードプロパティ(`build.march=rv32ec`等)で注入
- openwch公式(`openwch/arduino_core_ch32`、881行): 7ボード+`menu.pnum`(STM32duino踏襲)だが**pnum合計12項目**でカバレッジが薄い(103 SKU中12)。`build.variant_h=variant_{build.board}.h`でSKUヘッダ切替、`build.march=rv32ecxw`(WCH独自XW拡張)を使用

### 5. 他コアの前例(2026-08-19確認)

| コア | ボード数 | 粒度 | SKU表現 | boards.txt生成 |
|---|---|---|---|---|
| STM32duino | 42 | シリーズGeneric+フォームファクタ | `menu.pnum`計1,366項目(最大153/ボード)。variantは同一ピンマップSKU群で共有(295個) | 半自動(variantとboards_entryはCubeMX系DBから生成、boards.txtへは手貼付) |
| earlephilhower/arduino-pico | 150 | 製品ボード単位+generic | ボード=製品(die 2種) | **完全自動生成(makeboards.py、手編集禁止)** |
| espressif/arduino-esp32 | 356 | 製品ボード単位 | ボード=製品 | 手編集(61,004行。IDEメニュー崩壊の実害issue #11798、ボード追加PR集中) |
| esp8266/Arduino | 38 | 製品ボード単位 | ボード=製品 | **完全自動生成+CIが手編集PRを拒否** |

Arduino platform仕様の確認事項:

- メニュー選択項目のサブプロパティは選択時にそのままbuild/uploadプロパティへ反映される → **メニューでvariant、ldscript、march/mabi、build.boardまで切替可能**
- FQBNは`vendor:arch:board[:menu=opt,...]`。**メニュー省略時は「boards.txtの先頭項目」が既定になる**(仕様書に明文なし、実装挙動)→ CI用FQBNは生成時の項目順固定が必要
- メニュー数・項目数の仕様上の上限はない。実害はesp32型の「ボード数」爆発側で発生している
- CH32の書き込みはWCH-Link経由でUSB VID/PIDはプローブのもの → **ボード自動検出(VID/PID)でSKU判別はできず、ボード細分化に自動検出上の利点はない**

## 構成案の比較(提案)

| 案 | 内容 | 利点 | 欠点 |
|---|---|---|---|
| A: SKUごと個別ボード(103) | esp32/pico型 | 型番でボード検索できる。FQBNが自明 | ボード一覧103行(esp32の実害方向)。boards.txt巨大化。SKU追加のたびにボード増殖 |
| **B: family単位ボード(11)+pnumメニュー** | STM32duino/openwch型 | 一覧11行。最大メニュー26項目(STM32duinoの153で実運用済み)。SKU追加はメニュー追加のみ。device-dataの階層(family→product)と一致 | 2段階選択。IDE 2.x検索がボード名しか見ない(→ボード名に主要型番を含めて緩和)。pnum省略FQBNは先頭依存 |
| C: series単位ボード(27)+pnumメニュー | 現行openwchに近い | メニューが2〜7項目と小さい | 「V002はどのseries?」という知識をユーザーに要求。27行と11行の差にUX上の利点が薄い |
| D: パッケージ単位ボード | − | variantと1対1 | ボード50〜80個+結局サイズメニューが残る。前例なし。ユーザーは型番で探す |

**推奨: 案B**(family単位11ボード+pnum全SKU)。運用ルールとして:

- pnum項目は**26型番すべて**を列挙する(ユーザーは自分のチップの刻印をそのまま選ぶ)。温度グレード違い(E8R6/E8R7)や同等品は**同じvariant/ld/ビルド定義へ写像**して成果物を共有する(選択肢は減らさず、ビルドの実体だけ集約)
- 市販ボード(nanoCH32V003等)は独立ボードにせずpnum項目として追加し(STM32duinoがBlackPillでやった方式)、需要が確認できたものだけ独立ボード化する
- boards.txt/variantは`ch32-device-data`の固定releaseから**完全自動生成**とし、esp8266方式(CIが手編集PRを拒否)を最初から導入する
- pnum項目の順序は型番昇順で固定し、既定(先頭)が生成のたびに変わらないことを保証する

### boards.txt生成イメージ

```
menu.pnum=Part Number
menu.clock=System Clock
menu.opt=Optimize
menu.upload_method=Upload method

# ---- generated: family=CH32V00X ----
CH32V00X.name=CH32V00X (V002/V004/V005/V006/V007/M007)
CH32V00X.build.core=arduino
CH32V00X.menu.pnum.CH32V006K8U7=CH32V006K8U7 (QFN32, 62K/8K)
CH32V00X.menu.pnum.CH32V006K8U7.build.board=CH32V006K8U7
CH32V00X.menu.pnum.CH32V006K8U7.build.series=CH32V006
CH32V00X.menu.pnum.CH32V006K8U7.build.march={family既定。R-04参照}
CH32V00X.menu.pnum.CH32V006K8U7.build.mabi=ilp32e
CH32V00X.menu.pnum.CH32V006K8U7.build.variant=CH32V00X/V006K8
CH32V00X.menu.pnum.CH32V006K8U7.build.ldscript=CH32V006_62K_8K.ld
CH32V00X.menu.pnum.CH32V006K8U7.upload.maximum_size=63488
CH32V00X.menu.pnum.CH32V006K8U7.upload.maximum_data_size=8192
```

variantフォルダはSTM32duino式に「同一ピンマップのSKU群で共有」する。同一性は`pins.csv`/`pin_functions.csv`から機械判定できる(提案)。

## 判断ポイント

- **FQBNポリシー**(Q-017): pnum必須をREADMEとCIサンプルで明示するか。`wch:ch32:CH32V00X:pnum=CH32V006K8U6`形式の安定性保証
- **ボード名**: IDE検索対策としてfamilyボード名に含める型番範囲(全部は長すぎる)
- **ldの正本**: SKUごと生成ldをvariantに置くか`build.ldscript`切替にするか。旧コアの「固定ld+表示だけのmemoryメニュー」は廃止する
- **例外SKUの表現**: CH32V006F4U6(V006名でV002相当)、CH32M007E8U7(QFN26C3)、CH32M006(zh版のみ)をpnum表示名でどう注意喚起するか
- **FLASH/RAM分割series**(V20x/V307/V407): pnumとは独立の分割メニューを設けるか、初期は既定分割固定か([R-02](evt-structure.ja.md)参照)
- 温度グレード統合の粒度: pnumを26項目とも出すか、ビルド同一品を1項目に畳むか(推奨は前者=全項目表示)

## 未検証事項

- IDE 2.xでpnum 26項目メニューの実UX(スクロール、検索性)
- `tables/products.csv`とfamilies.csvの型番数の完全一致検査(V20x系で17/16の不一致の疑いを1件観測。CSVのquote処理の可能性があり、device-data側の`check_tables.py`で照合すべき)
- named board(市販ボード)の初期収載範囲
- V00X以外のfamilyのSKU表の同水準の検証(V00Xと同じ手順をdevice-data candidates→devices昇格で進める)
