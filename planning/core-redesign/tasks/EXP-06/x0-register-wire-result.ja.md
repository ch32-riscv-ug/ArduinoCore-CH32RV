# N02 X0用途・I2C register engine・資源台帳の直接接続

2026-10-21。実装commit `380cb2e`。固定入力は [x0-register-wire-inputs.json](../../x0-register-wire-inputs.json)。
[N01](x0-wire-result.ja.md)の同じ用途処理を、既存Master／Manager／Managed／Domainへ接続した。
これらの既存header、renderer、N01の意味処理は変更していない。
製品コア・USB／Networkを編集せず実験領域へ隔離した。公開APIや進行方式の採用ではない。

N02の直接回収部分の結果であり、N02全体の終了ではない。
通知queue満杯を含むevent接続、X035準備adapterとの用途結合は後続として残す。

## 接続と進行の境界

`RegisterX0` はR/A/B/C-static-batchそれぞれの既存frontendと、同じ `WireX0Meaning` を使う。
表示command／data、2回の計測、仮想touch、入力容量、PPS session・staleの意味はN01と同じ。
汎用EventExecutorとC-static-batchを同機能と見なす比較にはしない。

`X0RegisterWire` は3 client／専用3 recordの既存Manager、Managed、Domain<2>と41 Bのcaller storageを持つ。
用途の受理packetは引き続き1件。33 B TX＋最大8 B RXのstorageを一つだけ借用する。
2件目をqueueに積める新しい用途契約へ変更したわけではない。
host観測用packet保持とcounterもobjectに含み、最小化した製品driverの費用ではない。

startでは周辺・SCL・SDAのlogical leaseを取得し、Masterをopenして送信byteをstorageへコピー、submitする。
pin準備の宣言がfalseなら自分のleaseだけを返して拒否し、受理数を増やさない。
先行requestや外部ownerが資源を保持するときも、用途側のready状態を残して再試行できる。

`poll()` は明示操作。一回にdispatchまたはMaster::stepを最大一回実行し、
終端ならboundedなreap／closeを行う。time入力・用途step・start／take内でpollを繰り返さない。
この試作では通常文脈の低水準pollと用途handlerの実行機会を分離した。
loop外／delayへ自動で組み込んだり、実IRQ backendを作ったりはしていない。

Masterが宣言した停止条件を満たした後、reapでbackend側の参照を回収しcloseでleaseを返す。
この時点でも外側storageは貸出中。用途がtakeするまで、結果と受信byteを保持する。
takeは対応owner、終端、出力容量を確認し、Managerから結果を一度だけ回収する。
Successかつissued／confirmed／receivedが要求量と一致した場合だけRXを用途へコピーする。
失敗した計測はN01どおり前値＋staleとなり、表示の途中失敗はframeを完成扱いしない。

取消要求だけではstorageや資源を返さない。queued取消も明示pollで終端を取り扱ってからleaseを返す。
実行中取消はMasterの停止・回収を待つ。終端後に取消で結果を書き換える操作は行わない。

## 検証した範囲

最終runはnative84件＋workspace3件、計87件成功。既存plugin由来warning174件。
初回82件も成功したが、最終87件へ重複加算しない。
不正clock初期値の検査を追加した際、値を拒否する前にDomainをchanging状態へ入れないguardを加え、
全native検査とABIを最終sourceで再実行した。

- 272 trace・974,132入力操作・3,338,336 MMIO／critical eventを保存。68入力群でR/A/B/Cの全観測行が一致。
- 独立whole-image／transaction oracleにより、用途の受理・PPS・表示・計測・staleを照合。
  nativeの実際のData register書込みも、7 bit addressからのaddress byte、pointer、command／payloadと比較した。
- BERR／ARLO／AF／OVR／PECERRに対応する5 flagを、command、data、bus word、current wordの各TX途中へ注入。
  prefix 0、末尾、dataの16 B地点など10条件×5 flag×4群で、issued／confirmed／received／flagsを照合した。
- queued／dispatch後の取消、二重取消、取引終端前の通常進行停止、M/S/Uと禁止文脈を検査。
  時刻を1000 tick進めるだけではI2Cが完了しないことも確認した。
- 別のManagerが同じDomainを取得し通信開始した後、用途側の旧結果をtakeした。
  peerのleaseが残り、旧takeではMMIOが一件も発生しないことをliteralで照合した。
- pin宣言falseでの拒否・lease rollbackと、終端未回収のまま公称PCLK宣言を48→12 MHzへ変える例を検査。
  これはRCC変更・実clockの確認ではない。
- seed 0〜9それぞれ10,000生成入力＋2400 tick分の明示drainを4群へ与えた。
  session、値変更、エラー、取消、poll、進行機会を混在させた。
- register fakeは旧Master検査から同じ文字列を抽出しhash固定した。
  全MMIO・critical操作時に、このdriverがleaseを持つこととDomainの利用状態をassertする。

peerとpin／clockの検査はliteralと全群一致による追加検査で、whole-image oracleの外部owner模型ではない。
全interleaving、実IRQの原子性、物理的な停止・参照消失を証明した扱いにはしない。
互換観測用のcorruptions counterが0であることだけをmemory保護の証明には使わない。

## host刺激と実機の区別

nativeの `wire` はhost専用の刺激macroで、SB／ADDR／TXE／BTF、RX FIFO、quietを与え、
各段階で明示pollを呼ぶ。targetへリンクする関数ではなく、利用者向けblocking APIでもない。
このmacroのraw値は一つの刺激中では固定。read間のsample世代や実変換の時刻を表さない。
待機時間・IRQ処理時間・I2C throughputを、このmacroの実行時間から導かない。

RX2 byteの通常読出しと失敗時の破棄は接続したが、今回のfault prefixはTX側。
RXの途中停止・2 byte読出し前後のerror、notify満杯、setup／routeとの結合はN02で引き続き検証する。
controller full init、INA calibration／CNVR／OVFによる品質判定、実touch機器はN01同様未決定。

## ABI費用

[36構成の表](x0-register-wire-size-matrix.csv)は、2 ABI×LTO有無×（empty＋4群使用＋4群未使用）。
target入口はvolatile register accessとcompiler barrierのみで、IRQ-off・資源の排他が外部前提。
barrierをIRQ lockとして扱わず、実起動・物理I/Oを実行するprobeではない。

LTOあり、単位B。Flashは.dataの初期値を含み、静的RAMは.data＋.bss。

| 対象 | 群 | Flash image | 静的RAM | 用途object | 最大単一frame |
|---|---|---:|---:|---:|---:|
| V003 | R | 6616 | 716 | 648 | 108 |
| V003 | A | 6624 | 716 | 648 | 108 |
| V003 | B | 7080 | 748 | 680 | 128 |
| V003 | C-static-batch | 7208 | 732 | 664 | 156 |
| X035 | R | 6280 | 716 | 648 | 128 |
| X035 | A | 6288 | 716 | 648 | 128 |
| X035 | B | 6784 | 748 | 680 | 112 |
| X035 | C-static-batch | 6880 | 732 | 664 | 192 |

全群のharnessは68 B。R/Aは.data 648 B＋.bss 68 Bで、.dataの大部分が用途object。
.suの最大単一frameはstack peakではない。独立stackは追加していない。
未使用16構成の追加image・静的RAM・用途objectは0 Bで、emptyのbinaryと一致。
allocator／constructor arrayは検出されなかった。登録済みobjectのRAM回収を意味しない。
初回と最終の36 ELF・imageは全て一致し、初期化の入力guard追加でこの固定入口の数値は変わらなかった。

N01のfakeより機能・harness・進行条件が増えているため、その差を同機能の方式比較には使わない。
同じ新backendを使うR/A/B/C間の構造費用として扱う。
startup、実IRQ、clock／pin準備、実PD、stack peak、保存予約を含むB1成立は未検証。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_register_wire.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_x0_register_wire.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-register-wire-abi>
```

`EXP06_X0_REGISTER_EVIDENCE` は未作成directoryを指定し、生成fake・入力・native実行物・gzip traceを保存する。
今回は短い専用TMPDIRを使用。詳細・hash・tool・archiveは [測定記録](x0-register-wire-measurements.json)。
移送archiveは `work/exp06-x0-register-20261021-evidence.tar.gz`、18,891,182 B、
SHA-256 `60fafe583e9b594241b408688819fdb21845b5106799df32290064fe0b208c56`。
remote CIや実機の成功ではない。conftestは追加していない。

次は同じ用途と停止・回収境界をevent配信へ接続し、通知満杯と解除・retryの影響を比較する。
実装不良修正と比較は通常モデルで続ける。破棄後callbackや発生元寿命の公開契約を変更する必要が出たら、
反例を保存してP5の判断へ渡す。
[教材案](x0-register-wire-exercise.ja.md)は利用者レビュー前。N02全体、P4全体、最終仕様は未完了。
