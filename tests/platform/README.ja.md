# platform

実機を使わない source・生成物・compile の契約検査を置きます。
現在の入口は test_source_locks.py で、固定 source の一覧と hash をオフラインで確認します。
upstream の由来、API 互換、CH32 compile、実機動作の保証ではありません。

toolchain を必要とする検査を追加する場合は前提を明示します。
生成物の確認と通常のテストはリポジトリを変更せず、build は隔離した作業領域に出力します。

## 実行モデルの未使用時リンク費用を調べる実験

`probe_execution_link_cost.py` は手動実行の比較実験です。通常の pytest 収集では実行しません。
固定済みの RISC-V compiler と未作成の出力先を明示します。

```sh
python3 tests/platform/probe_execution_link_cost.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/execution-link-cost
```

RV32E／ILP32E と RV32IMAC／ILP32 で、呼び出し無効、未定義 weak hook、空の hook、
archive 内の未使用処理、直接リンクした未使用処理、使用中の処理、constructor による登録を比較します。
`-Os`・セクション分離・`--gc-sections` を使用し、LTO は使用しません。
この実験は例外情報も無効化した独立した最小プログラムで、実際の core／CRT／Arduino ライブラリはリンクしません。
constructor のケースは `.init_array` を保持する linker による到達性の検査で、起動時の実行検証ではありません。

出力には入力ソース、全コマンド、compiler とスクリプトの hash、ELF、map、逆アセンブル、symbol、
section 一覧、`result.json` を保存します。Flash は生の binary の長さ（配置の隙間を含む）、
静的 RAM は `.data`＋`.bss` として計算し、baseline との差も出します。
stack／heap の実行時最大量、CPU cycle、遅延、コア全体の追加サイズ、H417 の動作は測りません。
出力 ELF は MCU に書き込める firmware ではなく、実機への接続・書き込みは行いません。

### EXP-00 の再現と記録

`record_execution_link_cost.py` は上記probeを変更せず、独立した2出力先で実行・照合する手動入口です。
このprobe専用で、通常のpytest収集ではbuildしません。Python 3.11以降を使います。

```sh
python3 tests/platform/record_execution_link_cost.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/core-redesign-exp00
```

18構成×2runの結果を、ELFから再取得したsection・symbol・逆アセンブル・binaryと照合します。
2run間では数値、symbol、ELF、binary、出力先を正規化したmap、archive抽出の一致を要求します。
差があれば失敗として記録し、入力やpath等の原因を確認します。Flashの2 byteの配置間隙もsection和と区別します。

出力は `run1/`・`run2/`、`*.audit.json`、`manifest.json`、`commands.json` と各コマンドのstdout／stderr、
`summary.json`、`sha256.json`、入力文書・probe・記録スクリプトのコピーです。
manifestはcommit・差分・入力hash、compilerと内部helper・binutilsのhash、ABI別libgcc、tool配布lockとcache archiveを識別します。
内部helperの版は単独起動で調べず、hashとdriverの版を別々に残します。libgccは本probeではリンクしません。
archiveがなければ未取得、あればlockとchecksumを照合します。toolの自動取得は行いません。

出力先は未作成に限定し、失敗時もsummary・失敗コマンド・ログ・途中成果物を残します。
元のprobe内で失敗した場合はそのコマンド一覧と親プロセスのstderrから追跡できます。
実行時のstack・heap peak・cycle・遅延はnullです。

記録の失敗・上書き・誤集計を防ぐボード不要検査は次で実行します。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/platform/test_execution_link_cost_record.py
```

今回の結果と別作業領域での再生成手順は [EXP-00の結果](../../planning/core-redesign/tasks/EXP-00/result.ja.md)を参照してください。

## P2 の対照ロジックのABI確認

`core_redesign/build_logic_controls.py` は、K0／X0のhost対照と同じC++ロジックを対象ABIへcompile/linkします。
それぞれLTOなし／ありを別条件にし、入力・コマンド・libraryのhashとELF等を記録します。
通常pytestでは実行しません。入口と対象範囲は [host対照のREADME](../unit/core_redesign/README.ja.md)を参照してください。
最小起動・IRQ・実I2C／PDを含まないため、完成アプリの容量・実機動作はこの成功で検証済みとは扱いません。

## Core再設計：独立reset／CRT

次は明示実行のlink・命令検査で、実機へuploadしない。製品coreのstartupも変更しない。

```sh
python3 tests/platform/core_redesign/build_minimal_runtime.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-runtime-link

python3 tests/platform/core_redesign/check_runtime_faults.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --evidence work/exp01-runtime-link --out work/exp01-runtime-faults
```

固定参照の自動取得を避ける場合は最初のコマンドへ `--sources 保存先` を指定する。
出力先は未作成の場所を使う。constructor本体やmain・周辺は命令harnessでは実行しない。
[結果・限界](../../planning/core-redesign/tasks/EXP-01/runtime-result.ja.md)を参照する。

## Core再設計：clock/time入口

```sh
python3 tests/platform/core_redesign/build_minimal_runtime.py --suite clock-time \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-clock-time-link
```

固定資料をhash検証し、clock・counter・変換の入口をlinkする。実MMIOやmain以降をharnessで実行しない。
[結果・範囲](../../planning/core-redesign/tasks/EXP-01/clock-time-result.ja.md)を参照する。

## Core再設計：I2C masterのregister入口

```sh
python3 tests/platform/core_redesign/build_minimal_runtime.py --suite i2c-master \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-i2c-link
```

8／32／64 byteのraw・保護付きbufferと未使用構成をlinkする。
clock・pin設定済み、周辺の排他的所有、IRQ-offを前提にした比較入口で、main以降や実通信は実行しない。
[検証範囲・結果](../../planning/core-redesign/tasks/EXP-01/i2c-master-result.ja.md)を参照する。

## Core再設計：保存領域の予約とimage

```sh
python3 tests/platform/core_redesign/check_flash_regions.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-flash-link

python3 tests/platform/core_redesign/check_flash_region_faults.py \
  --evidence work/exp04-flash-link --out work/exp04-flash-faults
```

独立linkerで予約0／1／2／4単位を生成し、ELF・bin・hexと想定link拒否を検査する。
後段は保存したartifactを改変して、検査が破損を検出できるか確認する。
製品linker・boards.txtを変更せず、uploadや実Flash操作も行わない。
[結果と再実行条件](../../planning/core-redesign/tasks/EXP-04/region-result.ja.md)を参照する。

## Core再設計：保存形式のhost比較記録

```sh
python3 tests/platform/core_redesign/compare_flash_formats.py \
  --out work/exp04-format-model
```

RAW／blob／数値keyの上書き・2 bank・完了印を条件付き媒体模型で比較し、
容量・媒体操作回数・中断後の読出しと再更新を記録する。出力先は未作成の場所を使う。
MCUのcode／RAM・時間や、実電断耐性を測定するものではない。
[結果と注入集合](../../planning/core-redesign/tasks/EXP-04/format-result.ja.md)を参照する。

## Core再設計：保存形式のC++ ABI

```sh
python3 tests/platform/core_redesign/measure_flash_store.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-store-abi
```

固定geometryのC++候補を、LTOなしの仮backend境界へlinkして容量・symbol・stack情報を保存する。
作業bufferはprogram単位に合わせる。未使用の宣言と、明示した保存領域の予約を分けて検査する。
実Flash・startup・MCU時間・stack peakの測定ではない。
[結果と範囲](../../planning/core-redesign/tasks/EXP-04/store-result.ja.md)を参照する。

## Core再設計：保存領域の予算付き読出し

```sh
python3 tests/platform/core_redesign/measure_flash_scan.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-scan-abi
```

読出し専用の同期対照、byte予算付きreader、共有所有台帳、未使用をlinkして費用を記録する。
媒体は仮実装で、実FlashやCoreのstartup・時間を検証しない。
[結果と所有条件](../../planning/core-redesign/tasks/EXP-04/scan-result.ja.md)を参照する。

## Core再設計：論理時刻とtimer domain

```sh
python3 tests/platform/core_redesign/measure_logical_time.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-logical-time
```

観測上限の宣言・推定・native64、端数、既知／不明停止、timer境界の算術候補をlinkする。
partの宣言counter幅に合う候補だけを測り、libgccと支援関数の費用も含める。
実source・sleep・時間・stack peakは検証しない。
[結果・変更前後・制約](../../planning/core-redesign/tasks/EXP-01/logical-time-result.ja.md)を参照する。

## Core再設計：取得buffer 2 block

```sh
python3 tests/platform/core_redesign/measure_acquisition_buffers.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp02-acquisition-NEW
```

V003／X035、LTO off／on、sample容量8／64／128を含む32 ELFを測る。
payloadだけの対照は所有管理を持たず、同等機能として順位付けしない。
probeは実機起動やADC／DMAの動作、時間・stack peakを検証するものではない。
[結果と条件](../../planning/core-redesign/tasks/EXP-02/acquisition-result.ja.md)を参照する。
