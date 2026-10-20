# EXP-06 K0の同等ロジック比較と容量変更

2026-10-20。入力HEAD `2627685`。
K0の論理frontend・設定・公開値を揃え、R/A/B/Cを同じ入力・進行機会へ接続した。
13本の固定K traceはP2から変更していない。物理I2C、起動、IRQ保護、完成firmwareのfit、実行時間は未検証。
EXP-06／P4全体を完了とする結果ではない。

## 比較の境界

共通なのはK0の意味であり、進行方式は分けた。heapと独立stackは使用しない。
frontendの刺激後にadapterが明示的にchangedを通知し、frontendからhandlerをinline実行しない。

| 群 | 設定の保持 | 通常進行 |
|---|---|---|
| R | 用途専用のarrayとhead／count | 明示処理。自動進行profileはadapterで同じ許可条件を実装 |
| A | 共通Queue | 明示処理。Rと同じ許可条件 |
| B | 共通Queue | WorkExecutorの集約予約。次の設定が残れば次巡へ再投稿 |
| C | EventExecutorの宛先FIFOそのもの | 設定payloadと集約bitを同じ宛先へ。handler直前に回収した設定を意味処理へ渡す |

Cは設定用Queueを別に足していない。inputの飽和accumulator、受信staging、公開値、読出しsnapshotは全群が持つ。
CのDeliveryのframeをhandler中だけ借り、保持先へpointerを残さない。
登録はfrontendの開始前に一度行う。この比較は稼働中のbegin再呼び出し・再登録を対象にしない。

Mは明示機会だけ、Sはsystemの自動機会、Uはsystemと参加userの自動機会。
未参加user、初期化前、setup／ISR／callbackの禁止条件も同じにした。
ここでのDelayは進行機会の入力であり、実delayやloopへの組込みを実装した意味ではない。

Rも比較用に新しく同じfrontendへ接続した対照で、P2の旧R objectや上流firmwareのサイズと同一ではない。
容量1の意味はP2の凍結したKnobOracleへ照合した。
共有アルゴリズムにより、候補ごとに異なる飽和・不正frame・snapshot処理で小さくすることを避けた。

## B2：完成設定を2件に増やす

受信stagingは1件のまま、完成した未反映設定の容量だけを1→2にした。
1回の通常進行で設定1件をFIFO順に取り出し、その設定を検査・適用して差分を回収する。
複数設定を同じhandlerでまとめて適用する別条件とは混ぜない。
この処理単位を測定前に固定し、R/A/B/C全群へ同じように適用した。

2件受理後の次の取引は拒否する。先の設定が空いた後の再送は受理できる。
Bの残件は自己投稿で次巡に残り、Cは残ったFIFOにより次巡にeligibleとなる。
容量変更で1回のhandlerが無制限に長くなる構造にはしない。
この処理単位を製品の設定処理に採用したという判断は行っていない。

## Host検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_k0_candidates.py -o junit_family=xunit1
```

ASan／UBSan付き82件成功。既存plugin由来のexperimental warning82件。

- 凍結したK trace13本×R/A/B/C＝52条件。入力飽和、設定不正・拒否、seq周回、snapshot等を照合。
- 容量1／2×4群の8条件。受理順、拒否、再送、最終位置と公開frameをliteralにも照合。
- system／未参加user／参加user×4群の12条件。M/S/U、loop外／delay、禁止文脈を同一入力で検査。
- 容量1／2×5 seedの10条件。各5,000生成選択から構成した同じtraceを4群へ与え、
  各入力後の12状態・wire返答と進行statusを独立oracleへ照合。最後の保留設定と読出しもdrainする。

容量2のoracleは、P2の意味処理と別のdequeで完成設定を保持する。
内部callback回数は一致条件にせず、frontendの受理・状態・返答を比較する。
通常処理の途中へ実IRQを入れた検査ではない。8 byte copyのatomicityとwire上のACK／NACKは残る。

## ABIの費用

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix k0 \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-k0-link
```

29ケース×2 ABI×LTO有無、116構成がlink成功。
4群の未使用object×4条件、計16行は追加Flash／静的RAM／init_arrayが0 byte、application symbolなし。
同じ容量・profile・ABI・LTOのR/A全24組で、Flash imageと静的RAMの差は共に0 byteだった。
このK0ロジック入口では共通Queueを使う追加費用が観測されなかった。
Arduino全体やあらゆるclassの費用がゼロだという一般化はしない。

RV32E・LTOなし・明示Mの例。単位byte。

| 群 | 設定容量 | Flash image | 静的RAM | 同容量RからのFlash増分 | RAM増分 |
|---|---:|---:|---:|---:|---:|
| R | 1 | 1152 | 96 | 0 | 0 |
| A | 1 | 1152 | 96 | 0 | 0 |
| B | 1 | 1420 | 120 | 268 | 24 |
| C | 1 | 1872 | 128 | 720 | 32 |
| R | 2 | 1176 | 104 | 0 | 0 |
| A | 2 | 1176 | 104 | 0 | 0 |
| B | 2 | 1444 | 128 | 268 | 24 |
| C | 2 | 1932 | 136 | 756 | 32 |

受理／読出し／公開byte等の観測、volatile操作入口を含むlogic-onlyプログラム。
空の比較入口はFlash30／RAM20 byte。上表の増分は空入口ではなく、同じ機能のRとの差を示す。
全行と空入口からの差は [k0-size-matrix.csv](k0-size-matrix.csv)、
入力・tool・ELF hashと全24組のR/A差は [k0-measurements.json](k0-measurements.json)。
stackは通常stack上のsnapshot・呼出しと実IRQを含めて未評価で、B1のfit成立には数えない。

host `work/exp06-k0-host-20261020/`、link `work/exp06-k0-link-20261020/` に入力と生記録を保存した。
移送archive `work/exp06-k0-20261020-evidence.tar.gz`、2,713,764 byte、
SHA-256 `921c7988e587f5295e696977802747df5932c9d2f15b7b2af9e6492f0e4a4e3e`。
command・終了値・stdout／stderr・map・symbol・disasm・section・.su・hashを照合した。

次は待機のproducer進行条件、X0の同等用途、残るruntimeと実共有を進める。
容量2の変更課題の機械検査は成立したが、利用者が予測・変更した学習結果のレビューは未実施。
