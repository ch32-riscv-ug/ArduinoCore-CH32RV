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
