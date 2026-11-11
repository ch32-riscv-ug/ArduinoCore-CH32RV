# CH32H417 の UHSIF の解析（途中経過）

CH32H417 の UHSIF（Universal High-Speed Interface、最大 125 MHz・8〜32 ビットの並列バス）を、ロジアナの取り込みに
使えるかを調べた記録です。WCH はレジスタを公開せず、EVT の `libUHSIF.a`（ソースなし）だけを配っています
（[wch-evt-binary-libraries.ja.md](wch-evt-binary-libraries.ja.md)）。2026-11-11 の時点の結果で、**取り込みはまだ
できていません**（最後の節）。

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

## 実機で確かめたこと

- ライブラリの master の設定で起動すると、状態機械は動き、状態 1 で相手の合図を待ちます（WAVEFORM_CTRL_STAT が
  0x01000301）。
- 自前の状態機械も動きます。内部クロックで、クロックを端子に出さず、制御線をすべて入力にしました。「常に真で自分に
  戻り、毎クロック PUSH_WQ」の 1 状態です。起動するとすぐに、16 KB のバッファーが次々に埋まります。
- 終わったバッファーを返しても（`UHSIF_Trans_Cfg`）、続きは流れません。DMA の準備（lambda 26）を待たずに積み続ける
  設計なので、スレッドが止まったものとみています（未確認）。

## 解けていないこと: 取り込んだ値が端子から来ていない

取り込んだ値は、どの試行でも同じ定数でした。16 ビットでは 0xFEF7、8 ビットでは 0xF7、32 ビットでは 0xFD49FEF7 です。
端子のレベルとは合いません。次の操作をしても変わりませんでした。

| 試したこと | 結果 |
|---|---|
| データ線を GPIO の入力（プルアップとプルダウン）にする / AF 出力（0x9）にする | 変わらず |
| データ線の向き（AD_CONFIG の DQ_OEN_CFG）を「常に入力」にする | 変わらず |
| alpha を全部立てる | 変わらず |
| CLK_OUT を立てる。PF11 から実際にクロックを出す（試験の間だけ） | 変わらず |
| VIO18 を 3.3 V にする | 変わらず |
| データ線の AF の番号（AFIO の AFLR/AFHR）を 0〜15 にする | 変わらず |
| LED の端子 PF0 / PF2（D6 / D8）を GPIO の出力で切り替える | INDR は変わるが、取り込みは変わらず |
| ライブラリの master の設定のまま、waveform だけを替える | 変わらず |
| PORT0〜15、PORT32〜47 の端子のレベルと比べる | どれとも合わない |
| UHSIF のブロックの残り（0x40038200〜0x40039FFF）を読む | 0x40038000 からの 0x200 バイトが 0x200 ごとに繰り返される（別名）。0x40039800〜は 0。隠れた設定は見当たらない |

端子から UHSIF への入力の経路が、どこかで有効になっていないと考えています。FX3 には、端子を GPIF と GPIO の
どちらに渡すかを選ぶ IO マトリクス（GCTL_IOMATRIX）が GPIF の外にあります。UHSIF にもそれに当たるものが別の
場所にある、というのが今の仮説です。ライブラリの master の動き（相手の合図で読む）でも端子から取れるかは、
相手がいないので確かめていません。

## 次にやること

1. ライブラリの master で、相手の合図（制御線の入力）を満たして読ませ、そのときに端子の値が入るかを確かめる。
   入るなら、その状態のレジスタとの違いが答えになる。合図は、GPIO の出力で作るか、CPU が操作できる lambda（30）を
   使う。
2. UHSIF のブロックの外（0x40038000〜0x4003BFFF の残り、RCC、PWR、AFIO の残り）で、ライブラリと自前の設定の
   差を探す。
3. 取り込めるようになったら、DMA の準備（lambda 26）を見る状態機械にして、取りこぼしの無い連続の取り込みと、
   クロック（UHSIFDIV と UHSIFSRC）ごとの上限を測る。
4. WCH に、レジスタの資料と、`libUHSIF.a` の再配布の条件を問い合わせる。

実験に使ったスケッチと手順は、リポジトリの外（作業用のディレクトリ）に置いています。ライブラリを含むので、
リポジトリには入れていません。
