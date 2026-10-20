# message／blockの機能除去と構造費用

2026-10-21。[所有・回復の専用試作](h417-ownership-result.ja.md)を残し、別header `h417_features.hpp`でbuild時の機能選択を比較した。
実装commitは`bb342a1`。[入力](../../h417-features-inputs.json)、[数値とhash](h417-features-measurements.json)、[全112構成](h417-features-size-matrix.csv)を保存した。
製品コア・USB・Flashを変更していない。実行方式や公開APIの採用はP5へ渡す。

## 切り替える範囲

message storageとblock storageをそれぞれ型引数で選び、無効な側は空のbaseになる。
message4枠・head／size、block2個・generation・Ready列は、有効な型だけに存在する。
disabled操作は出力を変えずfalseを返し、gateも取得しない。
実行器・Arduino・旧Fabricへ依存せず、headerがincludeするのは`stdint.h`だけ。

gate reference・domain・epoch・通知・回復状態は共通のまま。
messageとblockを別instanceとして作れば、この共通状態をそれぞれが持つ。
両方を一緒に作ることと、別instanceで作ることの回復domainや寿命の意味を同じと決めていない。

single／fixed／shared-accessの権限、通知ackと所有返却の分離、非周回ticket、回復中の受理閉鎖は専用試作と同じ。
機能を外しても静止宣言の必要maskは7のままにした。どの構成ならDMA等の静止を省略可能かは今回決めない。
domainはstorageの生存期間に対し一意という外部条件も変えていない。
実reset・NOLOAD・旧writerの停止を保証する部品ではない。

## Host検査

ASan／UBSan、警告errorでnative184件とworkspace規則3件、計187件が通過した。
conftest.pyは追加せず、fixtureはtest module内に置く。

- 両方有効な型で、既存79シナリオとその文字通りのassertionを再利用した。旧test moduleのシナリオは編集しない。
- 3所有規則×3除去構成で、disabled操作の拒否・出力保持と、有効な側の完全な交換を検査した。
- 3所有規則×3除去構成×8静止maskで、storage除去により回復条件が緩まらないことを確認した。
- 3所有規則×4機能構成×2 seedで300選択の生成traceを検査した。50選択ごとに回復と有効な側の完全交換を入れる。

独立したlist型の所有模型から、disabled操作を拒否し、存在しないblock観測を除くprojectionを使う。
C++の空base・ring index・配置は模型に持ち込まない。
184 traceの全91,154操作で、成功／失敗・出力保持・FIFO・状態・payload prefix hashを照合した。
trace数はnone／message／blockが各33、bothが85。bothのうち79本は既存シナリオである。

以前の専用試作と[host並列同期](h417-thread-result.ja.md)の入力は変更していない。
ただし新headerは今回直列検査のみであり、以前のTSan成功を新headerの並列検証として数えない。

## ABIと依存除去

GCC 14.3.0、`-Os`、V003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、LTO off／on。
empty・raw payload64／128／192・3所有規則×4機能構成・その未使用12構成の計112 ELFをlinkした。
実startup・IRQ・DMA・H417同期を持たない構造比較。

両profile・両LTOで同じ本体byte数だった。

| 機能 | payload B | Fabric B | payload以外 B | fake Gate B | 測定harness等込みstatic RAM B |
|---|---:|---:|---:|---:|---:|
| none、回復管理は使用 | 0 | 16 | 16 | 2 | 96 |
| messageのみ | 64 | 84 | 20 | 2 | 184 |
| blockのみ | 128 | 168 | 40 | 2 | 268 |
| 両方 | 192 | 236 | 44 | 2 | 356 |

messageだけならbothから152 B、blockだけなら68 Bの本体storageを除去できた。
static RAMの差には、出力に使わなくなったtoken／message等のharness symbolの除去とalignmentも含む。
noneで回復管理を呼ぶ構成の16 Bを「何も使わないと0 B」の証拠にしない。
オブジェクト自体が未使用の48構成は、Fabric／Gate symbolと追加RAMが0 Bで、image hashもそれぞれのemptyと一致した。
全112 ELFでallocator symbolなし・constructor arrayなしを確認した。

LTO onのimage B。各欄はsingle／fixed／shared-access。

| 機能 | V003 | X035 |
|---|---|---|
| none、回復管理は使用 | 528／528／528 | 496／496／496 |
| messageのみ | 888／896／896 | 832／832／840 |
| blockのみ | 1,592／1,608／1,632 | 1,536／1,552／1,568 |
| 両方 | 1,856／1,872／1,896 | 1,816／1,832／1,848 |

専用試作のbothと比較すると、本体RAMは236 Bのまま。
LTO onのimageはV003で+40／+40／+32 B、X035で+8／+8／0 Bだった。
型で機能除去可能にした場合でも、全部有効な構成のcodeが必ず小さくなるとは限らない。
入口・配置・生成codeを含む比較で、増分を特定のC++構文だけの費用と断定しない。
LTO offの値と全差分も測定JSON／CSVに記録した。

発行された最大単一stack frameは、全使用構成でV003 20–76 B、X035 64–128 B。
call chain・割込みを含むstack peakや実CPU時間ではない。
fake Gateの2 Bは実HSEM／atomic backendの状態・barrier費用を含めない。

## 再現と残る作業

```sh
EXP05_H417_FEATURES_EVIDENCE=work/h417-features-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_h417_features.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_h417_features.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/h417-features-abi-new
```

保存archiveは`work/exp05-h417-features-20261021-evidence.tar.gz`。
source・compiler／binary hash・184 full trace・JUnit・112 ELFとmap／disasm／stack情報・analysisを含む。
archiveのbyte数・hashは測定JSONを参照する。同名出力先は再利用しない。

残るP4作業は、新しいfeature型へのhost並列検査、実行器への接続、固定ownerと共有実行器の比較、
H417共有layout・起動handshake・実backend・DMA・片側reset。
messageとblockを別domainにするか、回復時の破棄をどう公開するかは、この試作から採用を決めない。
教材では、本体費用とharness込み費用、回復管理だけ使用と完全未使用、機能除去と両方有効時の増分を分けて読む。
人による課題実施・理解度レビューは未確認。
