# EXP-03準備：有限workと実行機会の独立検査

2026-10-20。入力HEAD `d08d444`。
EXP-01のI/O契約との結合前に、依存しないRUN-01／02の部分をhostとlink-onlyで検査した。
EXP-03全体、R/A/B/Cの完成比較、実機runtimeの成立を示す結果ではない。
方式・公開API・systemとuserの実際の割当は確定していない。

## 固定した実験条件

`work_executor.hpp` は固定対象数、関数pointer＋context、readyの集約、単一通常stackでの配達だけを持つ。
登録は明示、heap・独立stack・timer・tick・I/Oはない。通常直列文脈専用で、IRQ投稿の保護を含めない。

一巡の開始時に、保存した巡回位置からeligibleなreadyを件数上限までsnapshotへ移す。
選択分を全てreadyから外してからhandlerを呼ぶ。したがって、実行中のpostは選択済みの別対象への
投稿も含めて次の一巡へ残り、同じ対象はその一巡に一度だけ呼ばれる。
選択しなかったreadyは保持し、巡回位置を最後の選択対象の次へ進める。
上限0では何も消費しない。

解除は保留workを取り消す。indexを再使用しても世代が違うため、古いsnapshotを配達しない。
世代が尽きたslotは退役し、wrapで古いtokenを再有効化しない。
解除で静的領域が減るわけではない。実行器自体とcontextの寿命は呼び出し側が保つ。
handlerは正常に戻る条件であり、無限処理を中断したり、例外から未処理workを復旧したりはしない。

## 実行文脈表

この単位で検査する非再入profileを次に固定した。delayの時間待ちそのものは未実装であり、
`Delay` はdelay(0)／delay(n)が進行機会を与える場合の入口だけを表す。

| 入口 | M | S | U |
|---|---|---|---|
| 初期化後、通常文脈から明示turn | systemと全user | 同左 | 同左 |
| loop外、delayの進行機会 | 配達しない | systemだけ | system＋明示参加user |
| 初期化前 | NotInitialized、ready保持 | 同左 | 同左 |
| setup、ISR | Forbidden、ready保持 | 同左 | 同左 |
| callback内／一巡の再帰呼び出し | Reentrant、配達なし | 同左 | 同左 |

systemも入れ子にしない厳密な非再入条件。
systemだけの限定再入、深さ制限付き配達、待機wrapperは別比較として残す。
setupからの明示進行を許す製品方針も、ここでは採用していない。

## Hostの結果

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_work_executor.py -o junit_family=xunit1
```

ASan／UBSan付き41件成功。既存plugin由来のexperimental warningは41件。

- 対象数1／4／8×一巡上限1／4／8の9条件。100巡ずつ、自己・相互postが続いても有限件で戻り、
  巡回順を保つ。handlerからの3種類の進行機会は再帰配達しない。
- 1件だけreadyの場合の相互postは次巡へ残る。既に選択した未実行対象へのpostも消失しない。
- M/S/U、system／参加user／未参加user、初期化前、setup、ISR、callback、上限0を検査。
- 解除とindex再利用を同じ一巡の途中で行い、古い対象・新しい対象のいずれも古いsnapshotから呼ばない。
  8 bit世代で両slotを使い尽くし、退役も確認。
- 対象数3条件×10 seed×10,000操作をPythonの独立ready集合・巡回modelへ照合。

## Link-onlyの結果

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix work \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-work-link
```

14ケース×2 ABI×LTO有無、56構成が成功。
未使用4条件は追加Flash／静的RAM／init_arrayが0 byte、component／tokens symbolなし。
RV32E・LTOなしの一例を示す。単位byte。

| 入口 | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 28 | 12 | 0 | 0 |
| 1対象・上限1 | 402 | 48 | 374 | 36 |
| 4対象・上限1 | 682 | 132 | 654 | 120 |
| 4対象・上限4 | 722 | 132 | 694 | 120 |
| 8対象・上限8 | 726 | 244 | 698 | 232 |
| 4対象・上限4・delay S | 730 | 132 | 702 | 120 |
| 4対象・上限4・delay U | 742 | 132 | 714 | 120 |
| 4対象・上限4・delay M | 502 | 132 | 474 | 120 |

入口は明示登録、投稿、自己・相互投稿のhandler、volatileな刺激と観測を含む。
RAMには実行器だけでなく、呼び出し側が所有するtoken配列も含む。
delay Mは登録とpostがあっても配達を抑止する別条件で、便利さが同じものの速度比較にはしない。
全行は [work-size-matrix.csv](work-size-matrix.csv)、入力・tool・生成物hashは
[work-measurements.json](work-measurements.json)。

一巡のsnapshotは通常stack上へ置く。8対象・上限8・RV32E・LTOなしの `.su` は
`data_entry` 108 byte static、handler 20 byte static、post 8 byte staticを出した。
これは各関数frameのcompiler情報で、呼び出し列やIRQの重なりを含むstack peakではない。
対象数の増加が静的RAMだけでなくstackにも影響するため、完成用途で再度計上する。

## 記録と未完了

hostは `work/exp03-work-host-20261020/`、linkは `work/exp03-work-link-20261020/`。
最終入力コピー・command・終了値・ELF・map・symbol・disasm・section・.su・hashを保存し照合した。
移送archive `work/exp03-work-20261020-evidence.tar.gz`、772,948 byte、
SHA-256 `54aa5ae2b960d35792f5953c5d35cee4b678f457c9411e51d8420961be90f98f`。

I/O終端・cancel・buffer、宛先messageと完了保持、timerとclock、wait、実ISR共有、
独立stack対照、実機の時間・stack観測、完成用途への結合は未検証。
次の作業は [継続単位](next.ja.md)に記す。採否を決める高位モデルへの移行はまだ不要。
