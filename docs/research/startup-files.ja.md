# R-01: startupファイル(.S)の横断調査

調査基準日: 2026-08-19
関連: [Q-012, Q-031](../open-questions.ja.md)

## 調査目的

WCH EVTのstartupファイルはfamilyごと・派生ごとに複数存在する。以下を明らかにする。

1. startupファイルが何をしているか
2. バリエーションが何のために分かれているか(使い分け)
3. Board Managerのボード/メニュー選択で切り替える前提で、単一ファイル+プリプロセッサへ統合できるか

対象は`/home/mt/dev_wch/`配下の全familyミラー(CH32V003, CH32V006, CH32V103, CH32V205, CH32V20x, CH32V307, CH32V407, CH32X035, CH32X315, CH32L103, CH32M030, CH32H417)のEVT。

## 確認済み事実

### 1. ファイルの所在と種類

- 正本は各family `EVT/EXAM/SRC/Startup/`にある。family内の派生(V20xのD6/D8/D8W、V307のD8/D8C、H417のv3f/v5f)もここに並置される
- それ以外の場所にあるstartupは特殊example用: `INT/VectorInRAM/`(`*_vector.S`)、`USART_IAP/`、`APPLICATION/Motor/Electric_Fan/`、OS移植(FreeRTOS等はSRC正本の複製)
- OS移植ディレクトリ内のものは正本の複製であり、独自変更は確認していない(全数diffは未実施)

### 2. 全familyに共通する処理構造

すべてのstartupは同じ骨格を持つ。

```
.section .init          _start: (vector table先頭 or j handle_reset)
[.section .vector]      _vector_base: vector table(family差分の本体)
.section .text.vector_handler   全ハンドラを.weakで宣言し、無限ループ(1: j 1b)へ束ねる
.section .text.handle_reset     リセット処理:
    gp設定(norelaxで__global_pointer$) → sp設定(_eusrstack)
    → [highcode copy(あるfamilyのみ)] → .dataコピー → .bssクリア
    → CSR初期化(mstatus, 0x804ほか) → mtvec設定 → jal SystemInit
    → mainをmepcへ入れてmret
```

- `main`へ`mret`で入るため、privilegeモードと割込み許可はmstatus/CSR初期値で決まる
- C++のグローバルコンストラクタ(`__init_array`)は**どのstartupも呼ばない**。linker scriptにはinit_array/ctorsセクションが定義済みだが、呼び出しは利用側(旧コアではpatch対象だった)。新コアでは自前実装が必要

### 3. family横断マトリクス

| family | 正本ファイル | core | vector形式 | .wordエントリ数 | CSR初期化(値) | mstatus | highcode | stack size(ld) |
|---|---|---|---|---|---|---|---|---|
| CH32V003 | startup_ch32v00x.S | V2A (RV32EC) | .initのみ、mtvec=`_start\|3` | 38 | 0x804=0x3 | 0x1880 | あり | 256 |
| CH32V006 (V00X) | startup_ch32v00X.S | V2C (RV32EmC) | .initのみ、mtvec=`_start\|3` | 40 | 0x804=0x3 | 0x1880 | なし | 512 |
| CH32V103 | startup_ch32v10x.S | V3A | .init+`.vector`、**j命令テーブル**、mtvec=`_vector_base`(モードビットなし) | (j形式) | なし | 0x88 | なし | 2048 |
| CH32V20x | startup_ch32v20x_{D6,D8,D8W}.S | V4B/V4C | .init(j)+`.vector`(.word)、mtvec=`_vector_base\|3` | 63/70/70 | 0xbc0=0x1f, 0x804=0x3 | 0x88 | なし | 2048 |
| CH32V205 | startup_ch32v205.S | V3B | .initのみ、mtvec=`_start\|3` | 84 | 0xbc0=0x21, 0x804=0x7, 0xbc1=0x1 | 0x88 | なし | 2048 |
| CH32M030 | startup_ch32m030.S | V3B | .initのみ、mtvec=`_start\|3` | 46 | 0xbc0=0x21, 0x804=0x3, 0xbc1=0x1 | 0x88 | なし | 2048 |
| CH32V307 | startup_ch32v30x_{D8,D8C}.S | V4F | .init(j)+`.vector`、mtvec=`_vector_base\|3` | 104 | 0xbc0=0x1f, 0x804=0x0b | **0x6088**(FPU) | なし | 2048 |
| CH32V407 | startup_ch32v4x7.S | V3V | .initのみ、mtvec=`_start\|3` | 103 | **csrc 0x805,0x100**, 0xbc0=0x21, 0xbc1=0x01, 0x804=0x07 | **0x688**(VS=vector) | なし | 2048 |
| CH32X035 | startup_ch32x035.S | V4C | .init(j)+`.vector`、mtvec=`_vector_base\|3` | 55 | 0xbc0=0x1f, 0x804=0x3 | 0x88 | なし | 2048 |
| CH32X315 | startup_ch32x3x5.S | V3F | .initのみ、mtvec=`_start\|3` | 65 | 0xbc0=**0x123703E1**, 0xbc1=0x01, 0x804=0x07 | 0x6088(FPU) | なし | 2048 |
| CH32L103 | startup_ch32l103.S | V4C | .init+`.vector`、mtvec=`_vector_base\|3` | 69 | 0xbc0=0x1f, 0x804=0x3 | 0x88 | なし | 2048 |
| CH32H417 | startup_ch32h417_{v3f,v5f}.S | V3F+V5F | .init+`.vector`、mtvec=`_vector_base\|3` | 149 | v3f: 0xbc0=0x123703E1, 0xbc1=0x01, 0x804=0x07 / v5f: 0xbc0=0x1237B3E0, 0xbc1=0x07, 0x804=0x0F | 0x6088(FPU) | あり(×2領域) | (未確認) |

補足:

- `.wordエントリ数`は`.word`行の総数(予約0を含む)で、実IRQ数ではない
- mstatus値の意味: 0x1880 = MPP=Machine+MPIE(mret後に割込み許可)。0x88 = MIE+MPIE。0x6088 = +FS(FPU有効)。0x688 = +VS(vector unit有効)。**V2系だけmret前のMIE=0という差がある**
- CSR 0x804はQingKeマニュアルのINTSYSCR(HW stack/割込みnesting設定)に対応するとみられる(値の意味は一次資料照合が未了 → 未検証)。0xbc0/0xbc1/0x805はvendor固有CSRで、家系ごとに定数が異なる。**この定数群は「family固有の魔法値」としてそのまま保持するのが安全**
- V103のみvector tableが`j`命令列(mtvecモードビットなし)で、`.init`に意図不明のnop列+ebreakを持つ。最古の世代で、他とパターンが異なる

### 4. family内バリエーションの使い分け

diffで確認した結果、使い分けは次の4種類に分類できる。

| 種類 | 例 | 差分の実体 |
|---|---|---|
| **SKUライン差(D6/D8/D8C/D8W)** | V20x D6/D8/D8W、V307 D8/D8C | **vector tableのIRQエントリの有無のみ**(D8: +ETH/TIM5/UART5-8/OSC系、D8W: +BLE BB/LLE、D8C: +USBHS/ETH/CAN2/DVP等)。reset処理は完全同一 |
| **VectorInRAM(`*_vector.S`)** | V00X/V003/M030/V205/V307 | table本体を`.vector`セクションへ分離し、mtvecを`_vector_base`へ向けるだけ。RAM配置はlinker script側(`.vector`を`>RAM AT>FLASH`へ)。**V307/V20x等の最新正本はすでに`_vector_base`形式なので、正本とVectorInRAM版のコード差はゼロ**(ヘッダコメントのみ)。差が残るのは`.init`一体型のfamily(V00X等) |
| **アプリ固有** | V00X Electric_Fan | highcodeコピー(RAM実行コード)の追加のみ |
| **IAP用** | V003 USART_IAP | vector tableを先頭数エントリへ縮小(flash節約) |

H417 v5fのみ例外的に複雑: handle_resetでFLASH関連レジスタ(0x40022000)のbit0-1を立て、`.loadcode`をRAMへコピーしてから、RAM上の`_load_base`へ飛んで残り(highcode×2、data、bss、CSR)を実行する。dual core(V5F+V3F)の各コア用に2ファイルある。

### 5. 旧コアの扱い(参考)

`arduino_core_ch32_riscv_noneos`はEVTの.Sを`.inc`へリネームし(patch適用)、familyごとのwrapper `.S`が**固定の1バリエーションだけ**を`#include`していた(V20x→D6、V307→D8C)。つまりboard選択によるライン切替をしておらず、V20xではD8/D8W専用IRQ(ETH、TIM5、BLE等)がvector tableに存在しない状態だった。

## 統合可否の評価

**統合は可能で、実質的な障壁は「vector tableの記述方法」の一点に集約される**(提案)。

根拠:

1. reset処理の差は「CSR書き込みの定数」「highcode有無」「V5F特殊シーケンス」だけで、`#if`パラメータ化が容易
2. family内ライン差(D6/D8等)はIRQエントリの有無のみで、これも`#if`または生成で解決できる
3. Arduino platformの標準recipe(`recipe.S.o.pattern`)は`.S`をgccで処理するため、Cプリプロセッサ(`#if`/`#include`)が使える。boards.txtのメニュー選択から`-DCH32V20x_D8W`等を注入すれば切り替わる
4. VectorInRAMは、全familyを`_vector_base`形式へ正規化すればlinker scriptの選択(またはld内の条件)だけで実現できる。startup側の分岐は不要になる
5. WCH自身が1本のstartupで6 series(V002〜M007)を賄っている例(V00X)があり、「seriesごとに.Sを持つ」必然性はない

### 構成の選択肢

| 案 | 内容 | 利点 | 欠点 |
|---|---|---|---|
| A: 完全1ファイル | 全familyのtableを`#if`で1つの.Sに記述 | ファイル1つ、検索性 | 12 family分で1500行超の見込み。family追加のたびに全体が伸びる。reviewしにくい |
| B: 共通crt + family別table include | reset処理を共通`startup.S`にし、`#include "vectors_<family>.inc"`でtableだけ差し替え | 共通部と差分が分離。includeはboards.txtの`-I`やdefineで選択可能 | ファイルは複数になる(ただし機械的な内容のみ) |
| C: 共通crt + device-data生成table | Bのincludeを`ch32-device-data`から生成 | SKU正本と一致。IRQ名の転記ミスがない。新seriesはdata追加だけ | device-dataにIRQ一覧(番号順テーブル)が必要(現状の有無は未確認)。generator整備が前提 |
| D: C言語でtable記述 | ch32funやSTM32系の一部のように、vector tableをCの配列/属性で書く | プリプロセッサ・型・生成が自然。アセンブラはcrt最小限 | mtvecモード(絶対アドレス方式)と`.option norvc`相当の配置制御をCで保証する検証が必要 |

**推奨**(提案): 方向はB→Cの段階導入。まず共通crt+手書きincludeで2 family(RV32E系1つ+RV32I系1つ)を動かし、Q-012の実測(ELF検査、debug互換)を通す。device-dataへのIRQ table収載が決まったらCへ移行する。ユーザー要望の「1ファイル+プリプロセッサ」は共通crt部分で満たし、tableは「機械生成される差分」と割り切るのが保守コスト最小とみる。

### 統合startupのプリプロセッサ軸(案)

| 軸 | 由来 | 例 |
|---|---|---|
| family/コア世代 | CSR定数セット(0xbc0/0xbc1/0x804/0x805/mstatus) | `CH32_CORE_V2A`等、または値そのものをdefineで注入 |
| series/ライン | vector table内容 | `CH32V20x_D8W`(vendorヘッダと同名を流用) |
| FPU/VS有効化 | mstatus 0x6088/0x688 | `CH32_HAS_FPU`, `CH32_HAS_RVV` |
| highcode | コピーコードとldセクション | `CH32_HIGHCODE` (全familyで共通機能化可能) |
| VectorInRAM | ld側で`.vector`配置切替 | startup側は分岐不要(正規化後) |
| V5F特殊боot | loadcode+RAM実行 | H417対応時のみ。`CH32_LOADCODE` |

## 判断ポイント

- **Q-012の本丸**: tableを手書き統合(A/B)にするかdevice-data生成(C)にするか。生成にする場合、`ch32-device-data`にIRQ番号→名前の順序付き一覧を追加する必要がある(schema拡張の要否をdevice-data側と合意する)
- weak handlerの既定を無限ループのままにするか、fault情報を残すdefault handlerに置き換えるか(デバッグ性とサイズのトレードオフ)
- コンストラクタ呼び出し(`__init_array`)をstartupに入れるかSystemInit後のown crtに入れるか(旧コアpatchの主要因のひとつ。新コアでは必須機能)
- mstatus/INTSYSCRの初期値をEVT値のまま踏襲するか、Arduino向けに再定義するか(HPE/hardware stack/nestingはQ-021の実測後に決定)
- V103のj命令テーブルを他と同じ.word形式へ正規化できるか(mtvecモードの一次資料確認が必要)

## 未検証事項

- CSR 0x804/0xbc0/0xbc1/0x805の各bitの意味(QingKe V2/V3/V4/V5マニュアルとの照合)
- V2A/V2C(V003/V00X)で`_vector_base`分離形式+VectorInRAMが実機動作すること(EVTに例はあるが実測未了)
- OS移植ディレクトリ内startupの全数diff(正本と同一という確認)
- H417 v5fの0x40022000操作の意味(FLASHコントローラ有効化とみられるが要照合)
- 統合後のELFがWCH-Link/probe-rsのdebug・書込みで問題ないこと
