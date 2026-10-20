# EXP-01 clock切替の有限処理とSysTickの読出し

2026-10-20。入力作成時のHEAD `6eacd3d`、検査済み試作 `fa62a49`。
製品coreとUSB実装を変更せず、独立runtimeへclock／counterの比較入口を追加した。
実機の周波数、精度、起動、IRQ、MMIO順序・stallは未確認。
この結果をArduinoのmillis／microsや、完成した論理時刻providerの成立とはしない。

## 固定資料と範囲

[追加資料と入力](../../clock-time-inputs.json)、[part・容量・startup](../../runtime-inputs.json)を使う。
WCHの固定mirrorから次を参照し、URL・bytes・hashを保存した。

- V003：[system sourceの48MHz HSI分岐](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/ADC/ADC_DMA/User/system_ch32v00x.c)、[core header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Core/core_riscv.h)、[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003RM.PDF) §6.5.4。
- X035：[system sourceのSetSysClockTo48_HSI](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/ADC/ADC_DMA/User/system_ch32x035.c)、[core header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Core/core_riscv.h)、[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/datasheet_en/CH32X035RM.PDF) §7.5.5。

| 項目 | V003 | X035 |
|---|---|---|
| 48MHz要求 | HSIをPLLで2倍 | HSI、HCLK分周1 |
| Flash LATENCY設定 | 1 | 2 |
| SysTickのcounter | 32 bit | 64 bit |
| clock入力選択 | HCLK／HCLK÷8 | HCLK／HCLK÷8 |

所有と文脈は、clock利用者を停止したexclusive・直列操作を前提とする。
owned引数は呼出し側の宣言で、mutexや外部libraryの所有を自動検出する仕組みではない。
初期化前後を含む同時呼出し、ISR共有、H417のコア間共有は未検証。

## clockの有限step

Clock48はbegin→HSI ready待ち→PLL ready待ち（V003）→register確認→Completeという処理。
V003はHSI選択中・PLL停止の互換状態だけを受理する。Flashの待ち状態を先に設定し、
factory trimが有効ならHSITRIMへ反映し、HSI由来PLLを選んでreadyと切替状態を確認する。
X035はHSI ready後、Flash設定、HCLK分周、確認へ進む。

1回のstepは有限回のregisterアクセスで戻り、whileによるready待ち、callbackやpumpを含まない。
readyが来なければPendingのstageを維持する。実時間timeoutやWDTをこのclassは提供しない。
abandonは観測を放棄した状態にするだけで、PLL停止・旧clockへのrollbackや所有解放を自動実行しない。
切替要求後に放棄した場合、実際のclock状態は外側で再確認・回復する必要がある。

Completeはその時点のregister確認であり、実周波数を測った値や、その後のclock障害の監視ではない。
稼働中の再構成はこのclock試作では扱わない。

## counterとepoch

Counterは明示beginで停止中のSysTickを設定する。countとstatusを初期化し、比較値を最大へ置き、
STRE・STIEを0にしてfree-runningで開始する。常設tick IRQ・software timer・taskは追加しない。
HCLK引数は宣言した周波数。実際の周波数・calibrationを測定して保証する値ではない。

- low：両系列とも低32 bitを読む。
- sample：V003は低32 bitをuint64へ拡幅するだけ。X035はhigh／low／highを照合し、最大3回試す。
- highが一致しなければUnstableで、出力値を変更しない。再試行を無制限に行わない。
- stop／beginで同じobjectのepochを進め、旧epochをStaleとしてMMIO前に拒否する。閉じたcounterも読まない。

epochは同じ長寿命objectの範囲で使う。別objectや破棄後の再生成を跨ぐglobal tokenではない。
objectをcopy／moveせず、寿命終了前に外側が利用を止める前提。暗黙のdestructor停止は追加していない。
世代上限では再開始を拒否するコードを置いたが、32 bitの実反復で上限へ到達する検査は行っていない。
Readyはsampleが得られた意味であり、clockが進み続けている保証ではない。

## 時間変換を分けて測る

変換helperは32 bitの有効な経過tickと非zeroの宣言周波数から、切捨てのmicrosecond区間を返す。
可変周波数の64 bit演算、同じ式へ6MHz定数を渡す場合、uint32のtick÷6へ特化した場合を分けた。
uint32範囲のtick×1000000はuint64に収まるため、6MHzならfloor(tick×1000000÷6000000)=floor(tick÷6)。
固定式へ置き換えられるのはこの範囲とrateに限る。

変換は状態を持たず、fractionの繰越やwrapの伸長をしない。
1 tickを6回変換して足すと0、6 tickを一度に変換すると1になることも検査した。
従って変換済みの小区間を単純に加算するだけでは連続時刻を作れない。

V003では7 tickと2^32＋7 tickの観測が同じになる負の例も確認した。
低32 bitの間隔を十分短く観測できるという前提なしに、複数wrapの欠落は検出できない。
48MHz／8の宣言条件ならwrapは約716秒、HCLK直結なら約89秒。これは保証された呼出し間隔ではない。
生のcount÷6をそのままArduino micros()とする実装は、この実験の成果物ではない。
clock変更・sleep・debug haltを跨ぐ継続時刻、64 bit経過区間の汎用変換は別の比較として残す。

## 検証

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_clock_time.py tests/unit/test_workspace.py \
  -o junit_family=xunit1

python3 tests/platform/core_redesign/build_minimal_runtime.py --suite clock-time \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-clock-time-link
```

保存済みのhash一致参照を使う場合は `--sources 保存先` を指定する。出力先は未作成の場所を使う。
ASan／UBSan付き新規34件、配置規則3件の計37件が成功。
既存plugin由来のexperimental warning37件。tests以下へconftest.pyを追加していない。

ready不成立、拒否と放棄、factory trim、部分的なclock確認、begin／stop、旧epochとbusy、
32 bit wrap、high変化・再試行上限と出力保持をregister transaction単位で独立Python模型へ照合。
8 seed×2000操作を両系列へ与え、計32000操作を照合した。
固定・特化の式は境界6値と1000生成値を両系列で比較した。全uint32値の列挙ではなく、上の整数式と範囲の説明を併用する。

10条件×2 part×LTO有無、40構成がlinkとresetの命令検査を通過。
未使用clock／counterの8行は空構成とbinary一致、追加Flash／static RAMは0 byte、関連object symbolも除去された。
compiler選択のlibgccをhash付きで記録し、全40 imageのdisasmでF命令がなく、V003ではM/A命令・x16以上の使用もないことを追加確認した。
これはCPU全体・MMIO orderingの証明ではなく、確認した命令featureの範囲に限る。
共通runnerの既存runtimeも40構成・拒否12件を再検査し、40 binaryすべてが前回と一致した。

reset／vector／data・bssも含むimageだが、命令harnessはmain到達直前で止まる。
clock／SysTickの実MMIOやdiv helperをMCU上で実行した結果ではない。hostの操作数をMCU時間へ換算しない。

## 費用

全行は [clock-time-size-matrix.csv](clock-time-size-matrix.csv)、入力・tool・library・監査は [clock-time-measurements.json](clock-time-measurements.json)。
LTOなしの参考値。各セルはFlash image／static RAM、単位byte。操作・引数・観測の入口も含み、class単体の機械語サイズではない。

| 入口 | V003 | X035 |
|---|---:|---:|
| 空 | 264／0 | 328／0 |
| 48MHz要求の有限step | 748／28 | 612／28 |
| 低32 bit読出し | 664／36 | 648／36 |
| 系列の幅で読出し | 672／36 | 688／36 |
| 可変rate・汎用64 bit式 | 2096／24 | 1580／24 |
| 6MHz定数・同じ汎用式 | 2072／24 | 1560／24 |
| 6MHz特化・32 bit除算 | 428／24 | 356／24 |
| clock確認後のcounter開始 | 1032／44 | 948／44 |

このcompiler／flagsでは、定数を渡した汎用式も64 bit演算helperを残した。特化した32 bit式は小さくなったが、rateと有効なdelta範囲が固定される。
LTOで常にFlash・RAMが減るわけではなく、globalの配置・整列を含むRAM spanも記録した。


余分なclock object／counter objectを置いただけの未使用構成も別に調べた。
variable-rateとconstant-rateは64 bit演算helperの費用も含み、特化32 bit式と能力が異なる。
宣言rateの正しさ、wrap・fraction・epoch処理を省略してよいという判断にはしない。
全構成でstackを256 byte予約する。これはstack peakの測定ではなく、変換やlibrary呼出しの実peakは未確認。

## 再現と後続

host `work/exp01-clock-time-host-20261020-r3/`、最終link `work/exp01-clock-time-link-20261020-r2/`、既存runtime回帰 `work/exp01-clock-time-runtime-regression-20261020-r2/` に入力と生記録を保存。
コマンド・終了値・stdout／stderr・JUnit・ELF・map・symbol・section・逆アセンブル・.su、hashを照合した。
移送archive `work/exp01-clock-time-20261020-evidence.tar.gz`、5,681,067 byte、
SHA-256 `238b39eb532596939df167a573c6f5a5f1d605648524dd3898addf4624f4fbca`。HEADだけでなく、当時の試作を含むhash付き入力で再現する。

方式・公開APIの採否はP5へ残す。次は論理時刻の伸長と不連続、clock・sleepとの接続、
I2Cの実backendと所有・取消・停止、実IRQ・実周波数・精度・stackを別の証拠で検査する。
