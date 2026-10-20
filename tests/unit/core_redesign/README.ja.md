# P2 のhost参照モデルと対照ロジック

P2-host-1。製品APIの実装とは別に、用途の意味と入力を固定するためのR対照。
設定と一次資料は [P2の対象固定](../../../planning/core-redesign/p2-preparation.ja.md)を参照する。

| ファイル | 役割 |
|---|---|
| r_workloads.hpp | MCUにもcross compileする、heap・実行器を持たないC++の状態処理 |
| host_driver.cpp | text入力でfrontend操作を注入し、状態または描画byteを出すhost専用入口 |
| oracle.py | 独立したPythonの参照計算。描画は全frameを生成し、C++側はchunkで生成 |
| traces.json | P1-1から具体化した固定入力と回帰ケース |
| frame-golden.json | 4条件の1024 byte画像、hash、入力 |
| test_r_workloads.py | 固定trace、文字通りの期待結果、seed付き差分、全byte照合 |
| r_application.hpp | 表示・計測・タッチの共有bus、周期、PPSを結合したR対照 |
| app_oracle.py、test_r_application.py | 独立した取引ledgerとの照合、故障・保留・容量・公平性の検査 |

hostにg++が必要。通常pytestでMCUへの接続・書き込みは行わず、hostの一時作業領域にcompileする。
AddressSanitizerとUndefinedBehaviorSanitizerを有効化する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_r_workloads.py -o junit_family=xunit1
```

元のrendererやprotocol実装は取り込んでいない。glyphはこの比較のために作成した。
入力世代・受理・回収・表示の統計は検査用の観測情報を含む。
状態objectのサイズやlogic ELFを、そのまま製品コアの容量として使わない。

MCUのABIでのlink確認は手動で行う。出力は最小起動・IRQ・実周辺のないlogic ELFで、firmwareとしては使えない。

```sh
python3 tests/platform/core_redesign/build_logic_controls.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/p2-logic-controls
```

K0／X0の各1対象でLTOなし／ありを別にbuildする。
libcの必要なmemory関数とlibgccをlinkし、各archiveのhashも記録する。
入力コピー、全コマンド、失敗、ELF・map・逆アセンブル・symbol・`.su`等を出力先へ保存する。
stackの観測peak、cycle、周辺の動作、完成用途のfitはこの入口では判定しない。

結合X0を含む全host検査は `tests/unit/core_redesign/` を指定して実行する。
結合X0のABI確認はbuild入口に `--application` を追加する。X035・LTOなし／ありの2構成となる。
fake busは実行中1件と終端1件を同じ受理slotに保持し、回収前の再利用を拒否する。
周期が遅れたら最新1回へ集約し、画面が不変ならrevisionを増やさない。
clockの周回・実PD・実IRQ・物理busを確認した扱いにはしない。

## EXP-02 データ部品の比較

`data_parts.hpp` と `data_driver.cpp` は実行器を使わない比較試作。
固定queue／借用storageのqueue、部分操作・借用範囲付きbyte ring、全体受理のmessage buffer、
flag、semaphore、owner付きtry lock、世代付き固定block poolを含む。
使用文脈は直列実行に限定する。ISR・複数producer・コア間で共有するための保護はまだ含まない。
非自明な型と共有policyは、以下の独立比較を使う。

queueのWakeFlagは空→非空の遷移で短いflagを設定する。
ユーザーcallbackやwaitは呼ばない。consumerはdrain・条件再確認を行う必要があり、
このflagだけで割り込みとの間の待機登録競合が解決した扱いにしない。
poolは古いtokenを拒否し、世代番号が尽きたblockを退役させる。
世代の保護費用を含むため、indexだけを返すpoolと同じ保証として比べない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_data_parts.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp02-data-matrix
```

容量・payload・固定／借用／通知付き、RV32E／RV32IMAC、LTOなし／ありを分ける。
各部品の未使用objectもlinkし、constructor・登録表・symbolの保持を確認する。
16 byte payloadは全wordを観測し、flagはset／clear／takeを別操作にして、
測ろうとする状態やcopyを入口だけで消してしまう条件を避ける。
値は観測用入口・libc／libgccを含むプログラムの追加量で、class単独の理論費用ではない。

`owned_queue.hpp` はmove-only型の寿命、`shared_channels.hpp` はgate付きqueueとSPSCを扱う。
`shared_driver.cpp`／`test_shared_channels.py` で所有権ledger、IRQ登録境界の到着、
hostの複数producerとSPSCのcounter周回・payload公開を検査する。
gateによる保護とatomicの適合条件を混同せず、host成功で実IRQ／cacheを確認した扱いにしない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_shared_channels.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix shared \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp02-shared-matrix
```

shared matrixはX035のSDK命令に合わせ `zifencei` を明示する。
OwnedQueueのMCU入口は明示構築で、global destructor登録の費用は含まない。
TSanの別実行と一次資料・未確認条件は[結果](../../../planning/core-redesign/tasks/EXP-02/shared-result.ja.md)に記録する。

## EXP-03 有限workの準備

`work_executor.hpp` は通常直列文脈用の独立試作で、I/O・timer・mailboxを含まない。
`work_driver.cpp` と `test_work_executor.py` が有限snapshot、再投稿、公平性、M/S/U、
再帰抑止、解除・世代を検査する。製品APIやEXP-03全体の完成とはしない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_work_executor.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix work \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-work-matrix
```

通常stack上のsnapshotも含め、対象数の費用を観測する。
実行文脈と未検証は[準備結果](../../../planning/core-redesign/tasks/EXP-03/work-preparation.ja.md)を参照する。

## EXP-01 I/Oの所有model

`io_lifecycle.hpp` は物理I/Oを持たない独立した所有・終端のmodel。
`io_driver.cpp`、`io_oracle.py`、`test_io_lifecycle.py` で2 slot／2 buffer、
取消・停止・回収の区別、sticky通知、endと古い世代を比較する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_io_lifecycle.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix io \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-io-matrix
```

観測用FNVと所有検査を含むmodelの費用であり、製品driverの費用にはしない。
結果と残作業は[準備結果](../../../planning/core-redesign/tasks/EXP-01/io-preparation.ja.md)を参照する。

## EXP-03 期限model

`deadline_timer.hpp` は時刻を渡して明示的に期限判定する独立試作。
`timer_driver.cpp`／`test_deadline_timer.py` が周回、半周、周期集約、保留通知、stop／再設定を検査する。
Python側の時計は周回しない整数を使う。tickや自動callbackは持たない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_deadline_timer.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix timer \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-timer-matrix
```

正常な再設定で古い通知を置換する条件も含め、意味と未検証は
[準備結果](../../../planning/core-redesign/tasks/EXP-03/timer-preparation.ja.md)を参照する。

## EXP-03 宛先mailboxとI/O／timerの結合

`event_executor.hpp` は通常直列文脈用の固定宛先・固定FIFOと集約bitの試作。
`event_driver.cpp`／`test_event_executor.py` で有限snapshot、世代、M/S/U、満杯、fan-outと
I/O／timerの保持・配達前解除を検査する。IRQ投稿や公開APIは未確定。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_event_executor.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix event \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-event-matrix
```

payload全wordの整合性と、生成後に残った受理messageのdrainまで照合する。
結果・費用・未検証は[宛先結合の結果](../../../planning/core-redesign/tasks/EXP-03/event-preparation.ja.md)を参照する。

## EXP-06 K0のR/A/B/Cと設定容量変更

`k0_candidates.hpp` は同じfrontend・意味へ直接保持、共通Queue、work、宛先FIFOを接続する。
`k0_driver.cpp`／`test_k0_candidates.py` で凍結K trace、同じM/S/U機会、容量2のFIFO・再送を比較する。
Cは宛先FIFOを設定保持に使い、二つ目の設定Queueを常設しない。
1回に設定1件を適用する条件。frontendの開始前にbeginを一度行う。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_k0_candidates.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix k0 \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-k0-matrix
```

物理I2CやIRQのatomicityは未検証。
結果と観測の限界は[K0比較結果](../../../planning/core-redesign/tasks/EXP-06/k0-result.ja.md)を参照する。

## EXP-03 待機policy

`wait_policy.hpp` はloop専用同期wrapperと状態を残す非同期pollの独立試作。
`wait_driver.cpp`／`test_wait_policy.py` がA/B/Cの実host dispatchと、IRQ／通常／別コアのfake producerを接続する。
IRQ／別コアの刺激は直列化しており、実共有やcacheを証明しない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_wait_policy.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix wait \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-wait-matrix
```

link入口はpolicy単体のtoy predicateで、queue／実行器を含まない。
timeout観測、宣言する進行能力、非同期driverの観測窓と未検証は[待機結果](../../../planning/core-redesign/tasks/EXP-03/wait-result.ja.md)を参照する。

## EXP-06 X0のR/A/B/C-static-batch

`x0_candidates.hpp`／`x0_driver.cpp`／`test_x0_candidates.py` はPPS・表示・計測・タッチを同じ仮想I/Oへ接続する。
Cの追加条件は `static_mailbox.hpp` の固定1宛先・有限batchで、汎用EventExecutorとは分ける。
P1のタッチ50回／秒を20 tick周期で検査し、旧PREP-02の50 tick周期とは区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_candidates.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix x0 \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-x0-matrix
```

テストのfixtureは各テストmodule内に置き、tests以下へconftest.pyを追加しない。
[結果と比較の境界](../../../planning/core-redesign/tasks/EXP-06/x0-result.ja.md)を参照する。

## EXP-03 資源を保持した待機

`wait_dependencies.hpp` は明示した依存の有限検査とDrive guardの比較試作。
`dependency_driver.cpp`／`test_wait_dependencies.py` は同じ資源を実際のtryと検査が参照するhost模型を使う。
所有者不明と未参加user、直接／間接のwaiter依存、他actorの循環、非同期での明示返却を区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_wait_dependencies.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix dependencies \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-dependencies-matrix
```

実backendの依存を自動発見する機能ではない。NoKnownBlockを進行保証としない。
費用と検出範囲は[資源依存の結果](../../../planning/core-redesign/tasks/EXP-03/dependency-result.ja.md)を参照する。

## EXP-06 X0のcompile時機能除去

`x0_features.hpp`／`x0_features_driver.cpp`／`test_x0_features.py` は固定off／on、
タッチを除いた専用対照、旧runtime0／1を同じ意味へ照合する。旧X0のソースと測定値は保持する。
無効時のtouch bufferと3番目のbus枠を除き、固定型と実行時切替の能力を区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_features.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_data_parts.py --matrix x0-features \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-x0-features-matrix
```

[結果と比較条件](../../../planning/core-redesign/tasks/EXP-06/x0-features-result.ja.md)、
[教材用の変更課題](../../../planning/core-redesign/tasks/EXP-06/x0-feature-exercise.ja.md)を参照する。

## EXP-01 独立runtimeとGPIO入口

`runtime_gpio_driver.cpp`／`test_runtime_gpio.py` は実験用low-eight pinのregister transactionをFakeへ照合する。
実MMIOをhostから呼ばず、GPIO class自体の実機成立とは区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_runtime_gpio.py -o junit_family=xunit1
```

reset／vector／data・bssのELFと命令検査は明示的に実行する別のplatform実験。
[結果と手順](../../../planning/core-redesign/tasks/EXP-01/runtime-result.ja.md)を参照する。

## EXP-01 clock/time入口

`clock_time_driver.cpp`／`clock_time_oracle.py`／`test_clock_time.py` は、
clock ready待ちを有限のstepで進め、owned counterのepochと読出しを独立模型へ照合する。
実MMIOをhostから呼ばない。連続時刻の伸長やArduino millis／microsの成立とは区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_clock_time.py -o junit_family=xunit1
```

[結果](../../../planning/core-redesign/tasks/EXP-01/clock-time-result.ja.md)、
[時刻を読む課題](../../../planning/core-redesign/tasks/EXP-01/time-read-exercise.ja.md)を参照する。

## EXP-01 I2C masterのregister入口

`i2c_master_driver.cpp`／`test_i2c_master.py`はFakeの2段受信場所・ADDR解除・送出BTFを使い、
write／read／repeated START、取消と回収、raw／保護付きbufferの差を検査する。
実MMIOや通信・IRQは実行しない。共有fixtureはこのファイル内で定義し、conftestは追加しない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_master.py -o junit_family=xunit1
```

[比較範囲と結果](../../../planning/core-redesign/tasks/EXP-01/i2c-master-result.ja.md)、
[貸出と小分け処理の課題](../../../planning/core-redesign/tasks/EXP-01/i2c-loan-exercise.ja.md)を参照する。

## EXP-04 Flash媒体と領域の模型

`flash_model.py`は消去済みprogram単位へ一度だけ試みる条件と、指定bitの中断を扱う。
`test_flash_regions.py`は物理粒度、予約・boot容量、整列、宣言されたupload消去範囲を検査する。
実Flash操作や電断耐性の検証ではなく、保存形式を比較するための条件付きhost模型である。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_regions.py tests/unit/core_redesign/test_flash_model.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

[比較条件と結果](../../../planning/core-redesign/tasks/EXP-04/region-result.ja.md)、
[容量と保持の課題](../../../planning/core-redesign/tasks/EXP-04/region-exercise.ja.md)を参照する。

## EXP-04 保存形式の条件付きhost模型

`flash_formats.py`／`test_flash_formats.py`はRAW、固定blob、最大2件の数値key snapshotを比較する。
上書き・2 bank・別program単位の完了印、指定bitの中断、再走査・再更新を検査する。
独占・直列の領域操作を前提とし、物理driverやRAW／keyのleaseはまだ含まない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_formats.py -o junit_family=xunit1
```

[比較結果と条件](../../../planning/core-redesign/tasks/EXP-04/format-result.ja.md)、
[更新途中を予測する課題](../../../planning/core-redesign/tasks/EXP-04/format-exercise.ja.md)を参照する。

## EXP-04 C++形式と初回／置換の中断

`flash_store.hpp`／`flash_store_driver.cpp`はprogram単位の作業bufferと設定snapshotを持つC++試作。
`test_flash_store.py`がPython模型へ、byte列・媒体操作・走査結果・世代・中断と再更新を照合する。
backendは失敗時も停止済みで、作業bufferを後から参照しない前提。実driverや領域leaseは含まない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_store.py tests/unit/core_redesign/test_flash_formats.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

[費用・初回保存・同期走査の範囲](../../../planning/core-redesign/tasks/EXP-04/store-result.ja.md)を参照する。

## EXP-04 byte予算付き走査と領域の所有

`flash_scan.hpp`／`flash_scan_driver.cpp`は最大32 byte／poll、共有台帳の重複拒否、ticketと結果の寿命を扱う。
検査は直列操作とcanonical物理addressを前提とし、書込み受理やkey変更への統合はまだ行わない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_scan.py tests/unit/core_redesign/test_flash_formats.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

明示的な証拠保存は`EXP04_SCAN_EVIDENCE=新しい出力先`を設定する。
検査に使ったnative binary・入力hash・build記録を保存し、既存の出力先は上書きしない。
[費用と条件](../../../planning/core-redesign/tasks/EXP-04/scan-result.ja.md)を参照する。

## EXP-01 論理時刻の端数と不連続

`logical_time.hpp`は32／64 bit sampleからの論理µs、端数、sourceと論理epoch、明示停止と再開を比較する。
`logical_time_oracle.py`は有理数と絶対期限で参照し、timer境界・推定・overflow・失敗時の出力保持も照合する。
物理の観測上限・周波数・sleep保持を測るものではない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_logical_time.py tests/unit/core_redesign/test_deadline_timer.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

検査時のnative証拠を保存する場合は`EXP01_TIME_EVIDENCE=新しい出力先`を指定する。
[結果と必要な外部条件](../../../planning/core-redesign/tasks/EXP-01/logical-time-result.ja.md)を参照する。

## EXP-02 取得buffer 2 block

`acquisition_buffers.hpp`は直列のfake生成元と利用側の所有を比較する。実ADC／DMAへの接続は含まない。
取消要求・停止宣言・結果保持・貸出・返却を区別し、8／64／128 sampleと生成元先行を検査する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_acquisition_buffers.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

検査時のnative binaryと入力hashを残す場合は`EXP02_ACQUISITION_EVIDENCE=新しい出力先`を指定する。
[結果と入力契約](../../../planning/core-redesign/tasks/EXP-02/acquisition-result.ja.md)、
[所有と分割処理の課題](../../../planning/core-redesign/tasks/EXP-02/acquisition-exercise.ja.md)を参照する。

## EXP-06 V006休止の待機境界

`sleep_boundary_model.py`は5手順と3 backend条件の有限順序を列挙し、通知が残ったまま眠る反例を記録する。
実WFI／WFE・SDK・MCUのemulatorではなく、必要なbackend条件を明示する比較模型。
同じ入力ファイルのLSI範囲とAWU候補から理想的なカウンタ時間幅も比較する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_sleep_boundary.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

[資料・反例・未検証範囲](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-result.ja.md)と
[予測課題](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-exercise.ja.md)を参照する。

## EXP-06 休止準備とI/O・時刻の結合

`sleep_coordinator.hpp`は既存IoSlot・論理clock・timerを、明示2参加者／任意registry容量2／4へ接続する。
cancel後の明示回収、input・期限通知、既知／不明復帰、部分再設定・失敗retryを比較する。
入口と再設定はfakeの能力宣言であり、実V006 adapterやISR共有の実装ではない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_sleep_coordinator.py tests/unit/core_redesign/test_sleep_boundary.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

検査に使ったnative binary・入力snapshot・hashを残す場合は`EXP06_SLEEP_FLOW_EVIDENCE=新しい出力先`を指定する。
[費用と保証範囲](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-flow-result.ja.md)と
[小さな状態へ分ける課題](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-flow-exercise.ja.md)を参照する。

## EXP-06 timerなし構成と段階失敗

`Coordinator`の期限依存を`NoDeadline`で除き、入力wake・clock・I/O所有は維持する。
`sleep_fault_driver.cpp`は操作後にWaiting／Failedを返すadapterと、Quietでの論理clock喪失を刺激する。
IRQ共有、実bus故障、転送中の全タイミングとの組合せは含まない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_sleep_coordinator.py tests/unit/core_redesign/test_sleep_faults.py \
  tests/unit/test_workspace.py -o junit_family=xunit1
```

native証拠を残す場合は`EXP06_SLEEP_FLOW_EVIDENCE=新しい出力先1`と`EXP06_SLEEP_FAULT_EVIDENCE=新しい出力先2`を指定する。
binary、入力snapshotとhashを保存する。[能力差と費用](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-features-result.ja.md)を参照する。

## EXP-06 生きたleaseと準備／中止の失敗

`sleep_fault_driver.cpp lease ...`は、片方／両方の転送中にPrepare／Abortを失敗させる。
終端だけでは回収されないこと、逆順回収、古いticketが次の要求を解放しないことを432条件で追加検査する。
runtimeは変更せず、ABIの再測定件数にも加算しない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_sleep_faults.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

native証拠は`EXP06_SLEEP_FAULT_EVIDENCE=新しい出力先`で保存する。
[結果・所有の境界](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-lease-fault-result.ja.md)と
[順序を予測する課題](../../../planning/core-redesign/tasks/EXP-06/v006-sleep-lease-fault-exercise.ja.md)を参照する。
