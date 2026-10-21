# N04 二つの通常stackと明示的な継続

2026-10-22。実装commit `1944a4d`。
入力は[two-stack-inputs.json](../../two-stack-inputs.json)、共通入力列は[two-stack-traces.json](../../two-stack-traces.json)。
製品コア・既存実行器・WCH portは変更せず、実験用RV32I portを追加した。
方式の採用ではない。

## 2 stackで何を残すか

同じ4個の容量1 slotで、入力待ち→要求→完了待ち→表示を記述した。
backendは通常側の同じ関数を使い、各明示turnはworkflow→backendの順に進める。

- 継続型：workflowはphaseと保留値を8 Bのobjectに残して戻る。
- 別stack型：workflowは同じ処理をlocal変数とwhileで書き、待つと通常側へyieldする。
  後の明示resumeから元の呼出し途中へ戻る。

別stack型は通常側とworkflowの2本だけで、登録task列・idle task・tick・preemptionを持たない。
通常側を止めるとresumeもbackendも止まる。入力・完了は明示的にofferし、満杯なら拒否を数える。
slotの共有は単一コアの直列条件。IRQ／別コアと安全に共有できるqueueを実装した意味ではない。

## 実験用portの契約

参照は固定した[psABIの呼出し規約](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/3095deae52ae3feecc54b5e185d14eb177e05d04/riscv-cc.adoc)。
source 53,129 B、SHA-256 `d64df459e5a74b77fa8b6dd02ecb79b81a8a031fcabaef24cfda8047ef767556`を保存した。
通常の整数呼出しで保持するs0〜s11、戻り先ra、spを保存し、gp／tpは共通の固定値として変更しない。
contextは56 Bの保存値＋padding、16 B整列の64 B object。2個で128 B。

soft-float RV32Iの通常関数呼出し境界だけを対象とし、IRQ／NMI／別コア／preemptionから切り替えない。
per-fiber TLS、専用gp／tp、FPU／vectorのcontextはない。RV32Eとhard-float ABIは明示的に拒否する。
fiberの開始関数は戻らず、破棄・cancel・例外unwind・再起動は未提供。
host例の停止時はuint32だけのlocalを残すfixtureであり、RAII所有者を含む一般task破棄を保証しない。
割込みが使うstack、割込み入口の保存、mask状態との整合は実機検証へ残る。

## 三種類の証拠を分ける

| 検査 | 確認した範囲 | 確認していない範囲 |
|---|---|---|
| Host workflow | 同じC++別stack処理と継続型を、同じ入力列と独立oracleへ照合 | RISC-V swapやMCUの割込み |
| 生成命令の整数模型 | 実ELFの106 B・29命令をdecodeし、contextの保存／復元と往復を確認 | 実CPU・trap・pipeline・切替時間 |
| X035 link配置 | 型・symbol・整列・除去・予約の費用 | startup・vector・実stack peak・安全容量 |

hostの切替はLinux `ucontext`で、host用stackは64 KiB。
この容量・host context・host切替時間をMCUの費用へ転用しない。
ASanはこのcontext APIを十分扱えないため、hostはUBSanだけで実行した。

## Hostの同一入力列

native17件＋workspace3件、計20件成功。既存plugin由来warning40件。
34 trace・202,082コマンド、17組すべてで両方式の公開状態が一致し、各々を独立Python oracleへ照合した。

- 正常、入力burst、早い完了、完了burst、sequence不一致、通常処理停止の6固定列。
- 1,000回yieldしても要求値を保持し、後の完了で表示するliteral条件。
- seed 0〜9の各10,000コマンド。入力・完了・turn・観測を組み合わせ、全slotのfull／保持値、拒否数、表示数、不一致数を照合。

入力の乱数値はuint32全域。拒否を処理済みとは数えず、turnがない期間に裏で処理しない。
ここで一致したのは継続型と同じC++ workflowを動かすhost別stack型。
[WCH FreeRTOS比較](stack-port-result.ja.md)のkernelを同じtraceで実行した結果ではない。
FreeRTOSはISRからのwake等で進行機会が異なるため、同一列を与えるだけで同じ観測境界になるとは仮定しない。

## RV32I swapの整数模型

保存14命令、復元14命令、returnの29命令。圧縮命令を含み106 B。
各stack容量・LTOの使用6行から取り出したswap bytesは同一。
同一codeについて1,000 seed、各2回のswapで、callee-saved・ra／sp・gp／tp・context周辺のsentinelを照合した。
同じcodeの再検査を異なる6,000 seedの証拠へ増やさない。
別stack上で保存レジスタ値を変えた後も、元のcontextへ戻れることを確認。
s11の復元offsetを誤らせたbytesはlayout検査で拒否した。

decoderはこのswapに必要なload／store／returnだけを扱う有限模型。
通常のC++処理全体や割込みを実行するemulatorではない。
ターゲット向けの呼出し・復帰・実register保持はN09で改めて確認する。

## 構造費用

[18構成](two-stack-size-matrix.csv)はX035、LTO有無、継続型と別stack256／512／1,024 Bの使用／未使用＋empty。
未使用8行はimageがemptyと一致し、追加静的RAM／用途objectは0 B。
共通の通常stack予約1,024 Bはemptyにも残る。
allocator・constructor arrayは検出されなかった。

X035、LTOあり、単位B。

| 構造 | Flash image | 静的RAM | 継続state | context合計 | 別stack配列 | 通常stack予約 | RAM総予約 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 継続型 | 288 | 108 | 8 | 0 | 0 | 1024 | 1132 |
| 別stack256 B | 520 | 496 | 0 | 128 | 256 | 1024 | 1520 |
| 別stack512 B | 520 | 752 | 0 | 128 | 512 | 1024 | 1776 |
| 別stack1024 B | 520 | 1264 | 0 | 128 | 1024 | 1024 | 2288 |

用途object60 Bと共通harness40 B、整列を含む。fiber配列の始点／終点は16 B整列。
512 Bの比較では、継続stateを外してcontextとstack配列・整列が増え、RAM総予約差は644 B。
stack容量は候補値で、安全な最小量を測ったものではない。
通常側の1,024 B予約も宣言値で、実IRQを含む安全性は確認していない。
`.su`はcompile時の関数frame情報で、全call経路や観測peakではない。
switch cycle・wait latency・stack peakはnullのまま。

FreeRTOS側とはqueue型・scheduler機能・観測harness・IRQ予約規則が異なる。
表の差を同じ保証を実現する方式の純粋な追加費用へ読み替えない。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_stack_pipeline.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_two_stack.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-two-stack-abi>
```

`EXP03_TWO_STACK_EVIDENCE`へ未作成directoryを指定するとhost入力・実行物・traceを保存する。
[測定記録](two-stack-measurements.json)にprimary source・入力hash・JUnit・ELF・整数模型・archiveを残した。
RV32Eとhard-floatの拒否buildは別の2件で、使用18構成の成功数へ加算しない。
[教材案](two-stack-exercise.ja.md)は人による評価前。

N04は継続型／2 stackのhost意味と整数port・linkまで追加した。
次はFreeRTOSへ共通入力列を与えるfixture境界を整える。
burst中にtaskを進めるか、wake後どこで観測するかを明示し、未検証のkernel挙動を模型の一致で埋めない。
実切替・待機・IRQ・peakはN09、公開範囲と寿命・破棄はN10／P5へ残す。
