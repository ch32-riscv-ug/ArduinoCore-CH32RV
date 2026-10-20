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
