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
