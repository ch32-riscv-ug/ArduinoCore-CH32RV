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
