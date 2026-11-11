# CH32H417 の UHSIF の解析

CH32H417 の UHSIF（Universal High-Speed Interface、最大 125 MHz・8〜32 ビットの並列バス）を、ロジアナの取り込みに
使えるかを調べた記録です。WCH はレジスタを公開せず、EVT の `libUHSIF.a`（ソースなし）だけを配っています
（[wch-evt-binary-libraries.ja.md](wch-evt-binary-libraries.ja.md)）。2026-11-11 の時点の結果で、**ライブラリを使わずに、
16 本のデータ線を内部クロックで連続して取り込めました**（SYSCLK ÷ 2 で約 197 MS/s、÷ 1 で約 390 MS/s を SRAM へ）。
自前の設定のスケッチは [tools/diagnostics/h417-bench/UhsifCapture](../tools/diagnostics/h417-bench/UhsifCapture/) です
（ライブラリのコードも FX3 SDK の文も含みません）。

調べ方:

- `libUHSIF.a` の逆アセンブル（コードは約 3 KB）。
- 実機（ベンチの H417 評価ボード、QEU6）で、ライブラリをリンクしたスケッチを動かし、レジスタを読み出しました。
  ライブラリが呼ぶ `GPIO_Init` は、端子に触らず引数を記録するだけの関数に差し替えています。出力の端子は駆動していません。

## 正体: Cypress FX3 の GPIF II と同じ作り

UHSIF のレジスタの並びと意味は、Cypress（Infineon）EZ-USB FX3 の GPIF II と PIB（socket と descriptor の DMA）に、
レジスタ単位で一致します。照らし合わせには、FX3 SDK のヘッダー（`gpif_regs.h`、`sock_regs.h`、`cyu3gpif.h`）を
参照用に読みました。FX3 SDK は Cypress のライセンスのもので、このリポジトリには写していません。

一致を確かめた点:

- 設定のレジスタは 76 個（0x000〜0x12C）で、FX3 の GPIF_CONFIG〜GPIF_BETA_DEASSERT と同じ範囲・同じ順です。
  ライブラリの表の値も FX3 の GPIF II Designer の出力と同じ形で、一部は値まで同じです（SERIAL_IN/OUT_CONFIG、
  THREAD_CONFIG、BETA_DEASSERT）。
- `UHSIF_Start()` は +0x118 と +0x11C のビット 0 を立てます。FX3 では WAVEFORM_CTRL_STAT.WAVEFORM_VALID と
  WAVEFORM_SWITCH です。
- 状態の記述子は、1 状態につき 12 バイトの左右 2 組で、フィールドの並び（次の状態、遷移関数への入力 FA〜FD、
  関数の番号 F0・F1、alpha、beta、繰り返しの回数、VALID）が FX3 と同じです。
- 入力（lambda）の割り当ても同じです。LAMBDA_STAT（+0x10C）の bit 28（常に 1）が実機で立っています。
- DMA は、RAM に置く 32 バイトの descriptor（バッファーのアドレス、sync、次の番号、大きさ）と、socket のレジスタ
  （SCK_DSCR、SCK_SIZE、SCK_COUNT、SCK_STATUS、SCK_INTR、SCK_INTR_MASK）です。descriptor の番号は
  「アドレス ÷ 32」です。

FX3 と違う点:

| 項目 | FX3 | UHSIF |
|---|---|---|
| ブロックの先頭 | 0xE0014000 | 0x40038000（RM の番地表にある 0x40013400 ではない） |
| 遷移関数（16 ビットの真理値表 × 32） | +0x130 | +0x180（+0x120〜0x17F はその別名に見える） |
| waveform のメモリ（左 / 右） | +0x1000 / +0x2000 | +0x2000 / +0x3000（0x4003A000 / 0x4003B000）、16 バイトおき |
| socket | PIB の socket | 0x40039000 + 0x100 × バッファー + 0x40 × 線。番号は 4 × バッファー + 線 |
| クロック | GCTL | RCC_CFGR2（+0x30）の UHSIFSRC（SYSCLK / PLL / USBHS の PLL / ETH の PLL）と UHSIFDIV |

## ライブラリの API がすること

| 関数 | すること |
|---|---|
| `UHSIF_GPIO_Init(mode, port_rm, clk_rm, div, width)` | RCC の UHSIF のクロックを有効にし、UHSIFDIV に div を書く。AFIO の PCFR1 に port_rm と clk_rm を書く。端子を `GPIO_Init` で設定する（master 16 ビットでは、データ 16 本が AF 出力、クロック PF11 と制御線が出力、いくつかが入力） |
| `UHSIF_Cfg()` | モード（master、slave の FPGA、slave の SOC）ごとの表で、76 個のレジスタ、遷移関数、waveform を書き込む（master は 11 状態、slave は最大 199 状態） |
| `UHSIF_Line_Cfg(line, dir, n, buf, wm)` | バッファー n 個（各 16 KB）の descriptor と socket を作り、THREAD_CONFIG に socket を結ぶ |
| `UHSIF_Start(on)` | WAVEFORM_VALID と SWITCH を立て、割り込みを有効にする。off は RCC で UHSIF をリセットする |
| `UHSIF_Trans_Cfg(line, idx, size)` | 終わったバッファーの socket を、もう一度動かす |
| `UHSIF_Reg_Cfg(mode, n, value)` | 0x40038000 + 4n に書いて読み返す（n は 0〜75） |
| 割り込み | バッファーが 1 つ終わるたびに、スレッドの socket を次（+4）に進め、`UHSIF_LineN_IN_Callback(バイト数)` を呼ぶ |

- ライブラリの割り込みの処理は、レジスタを自分で退避しない作りです（HPE が前提）。このコアの H417 は HPE を有効に
  しているので（INTSYSCR 0x07）、そのままベクターにつながります。
- ライブラリは soft-float の ABI、このコアの H417 は ilp32f です。ライブラリに浮動小数点のコードは無いので、
  ELF の ABI のフラグを書き換え、属性のセクションを外せばリンクできます。WCH 独自の命令（Xw）も含みます。

## 状態機械の意味（実機で確かめたこと）

FX3 の資料に書かれていない、または読み違えやすい点です。

- 状態の出力（alpha、beta）は、その状態に**入る遷移**で出ます。自分に戻る遷移（同じ状態に留まる）では、alpha は
  もう一度は出ません。
- **alpha の bit 2（左の辺）が、データ線の値をラッチします。** beta の bit 7（PUSH_WQ）は、ラッチした値を DMA に積む
  だけです。ラッチ無しで PUSH_WQ だけを出すと、前にラッチした値（リセットをまたいでも残る）が積まれ続けます。
  以前の「取り込んだ値が端子と合わない定数（0xFEF7 など）」は、これでした。
- 端子からの経路に、別の有効化（FX3 の IOMATRIX のようなもの）は要りません。データ線は GPIO の入力のままで取れます
  （AF の番号も不要）。
- 1 サンプル / クロックにするには、2 つの状態（1 と 2）を毎クロック行き来させ、両方への遷移で alpha の bit 2 と
  PUSH_WQ を出します。
- lambda の 0〜15 は制御線の端子の値、26 は DMA の準備（DMA_DREADY）、28 は常に 1 です。遷移関数の表の番号は
  「a + 2b + 4c + 8d」（a〜d は FA〜FD の値）です。
- waveform の 1 状態（16 バイト）のビットの並び:

| 語 | ビット | 中身 |
|---|---|---|
| w0 | 7:0 | 次の状態 |
| w0 | 12:8 / 17:13 / 22:18 / 27:23 | FA / FB / FC / FD（lambda の番号） |
| w0, w1 | w0 31:28 と w1 0 | F0（左の遷移関数の番号、5 ビット） |
| w1 | 5:1 | F1（右の遷移関数の番号） |
| w1 | 13:6 / 21:14 | 左の alpha / 右の alpha |
| w1, w2 | w1 31:22 と w2 21:0 | beta（32 ビット） |
| w2 | 29:22 / 30 / 31 | 繰り返しの回数 / beta を戻す / VALID |

## DMA の続け方

- バッファー i は socket 4i で、descriptor は 1 つ（自分につなぐ）。スレッド 0 は、THREAD_CONFIG の下位 5 ビットの
  socket を使い、割り込みのたびに +4 進めます（ライブラリは 8 バッファーを前提にしています）。
- 埋まった socket をもう一度動かすには、SCK_INTR の 0x1E5（**SUSPEND と DSCR_IS_LOW も含む**）を 1 で消し、
  SCK_COUNT を 0 にして、SCK_STATUS の bit 30（GO_SUSPEND）を 1 回立てて戻します。SUSPEND を消さないと、8 バッファーで
  止まります（`UHSIF_Trans_Cfg` だけで続かなかったのは、これと、DMA の準備を見ない状態機械のためでした）。
- 状態機械は、lambda 26（DMA の準備）が 0 の間は状態 3 で待ちます。

## 速さ（2026-11-11、ベンチの H417、SYSCLK 約 400 MHz）

16 KB（8192 サンプル）のバッファーが 1 秒に埋まる数から出しました。入力は PF0（LED の端子）を CPU でゆっくり
切り替えたもので、端の数は切り替えの回数と合い、同じ値が続く長さはサンプリングの速さに比例しました。

| UHSIFDIV | クロック | バッファー / 秒 | サンプル / 秒 |
|---|---|---|---|
| 63（÷ 64） | 6.25 MHz | 762 | 6.24 M（端 6000 を取りこぼし無し） |
| 7（÷ 8） | 50 MHz | 6076 | 49.8 M |
| 3（÷ 4） | 100 MHz | 12111 | 99.2 M |
| 1（÷ 2） | 200 MHz | 24079 | 197 M |
| 0（÷ 1） | 400 MHz | 47616 | 390 M（UHSIF の仕様の 125 MHz を超える。SRAM へは流れた） |

- 確かめたのは遅い信号（約 0.6 µs ごとの切り替え）だけです。速い信号で値が正しいか（セットアップ・ホールド、16 本の
  ずれ）は、外の発振器でまだ見ていません。
- PC へ流す（USB SS）ときの上限は測っていません。200 MS/s × 16 ビットは 400 MB/s で、SS の実効に近い値です。

## 残っていること

1. 速い信号での確かさ。外の発振器や、OEP のプローブの出力で、50〜200 MHz の範囲で見る。
2. USBSS で PC へ流し続けるときの上限。
3. 右の辺（F1、右の alpha）の意味。今の設定は、右の辺を「DMA の準備が無いとき状態 3 へ」に使っています。
4. 外部クロック（CLK_SOURCE を外に）、トリガー（lambda の制御線で開始）、8 / 32 ビット幅。
5. コアのライブラリにする（CH32RVLogicCapture の H417 版）かどうかは別に決める。

試験の間、ライブラリをリンクした実験のスケッチは、リポジトリの外（作業用のディレクトリ）に置いています。
