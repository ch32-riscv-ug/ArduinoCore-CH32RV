# EXP-03 try・非同期再開・同期wrapperの進行条件

2026-10-20。入力HEAD `886ed38`。
RUN-03を、同じqueue結果を待つA/B/Cのhost adapterと独立した待機policyで検査した。
blocking APIの提供、RTOS、delay中の進行範囲を採用する判断は行っていない。

## 三つの操作

| 操作 | 進行と状態 |
|---|---|
| 即時try | Queueのpopだけ。空なら出力を変更せず戻り、producerを進めない |
| AsyncWait | start／期限／activeを保持。明示pollでtryと期限だけを確認し、producerや実行器をpumpしない |
| wait_for | 通常loop文脈だけの同期wrapper。空で正の時間待つときに、時計とproducerの進行可能性を確認してから、許可したDriveを呼ぶ |

AsyncWaitは通常callbackからもpollできる。setup／ISRはこの直列試作では拒否する。
取消と完了後はIdleとなり、後から来たqueueデータを再び消費しない。
不正な再armは既存の待機を変更しない。
待機を取り消すことと、I/Oの停止要求・実停止・buffer回収は別の操作であり、自動で連動させていない。

同期wrapperはsetup／ISR／callbackから即時Forbiddenで、既にqueueが非空でも消費しない。
この制約の下でも、callbackが即時tryや非同期pollを使うことはできる。
文脈・進行許可はhost adapterが供給しており、製品runtimeでの取得方法は未実装。

## 空で待つときのproducer条件

| producer | 明示pump | 自動機会M | 自動機会S | 自動機会U |
|---|---|---|---|---|
| IRQ | IRQが進行可能なら可 | 同左 | 同左 | 同左 |
| 別コア | 相手が進行可能なら可 | 同左 | 同左 | 同左 |
| 通常system | 可 | 不可 | 可 | 可 |
| 通常user・未参加 | 可 | 不可 | 不可 | 不可 |
| 通常user・参加 | 可 | 不可 | 不可 | 可 |

Mの明示pumpと、自動機会としてのdelayを区別した。
空で待ち始めるときにproducerを進められなければCannotProgress、時計が使えなければClockUnavailable。
期限到達まで回して偶然戻ったことを、producerが進められた成功とはしない。
既に値がある場合は未来のproducerを必要としないので、禁止文脈・不正期限の確認後に即時回収できる。
timeout=0は一度のtryだけで、空ならTimedOut、pumpしない。

IRQ／別コアはhostの外部刺激として直列に模擬した。
実ISR共有、atomic、cache、相手停止の検出が成立した証拠ではない。
通常producerはAの明示処理、BのWorkExecutor、CのEventExecutorを実際に経由する。
callback内から同期wrapperを試し、Forbiddenでqueueと出力値を保持し、再帰pumpしないことも確認した。

## 時刻とtimeoutの意味

相対期限は32 bitの半周未満。wrapと比較間隔は既存の時間契約に従う。
各観測でtryを優先し、tryに失敗した場合に期限を確認する実験条件とした。
Driveが期限をまたいだ後に値があればCompletedを返すが、経過時間も返して遅い観測を区別する。
例ではtimeout9、5 tickずつ進行、producer到達9で、実際の回収は10となる。
9で回収できたことにも、時間上限を守ったことにもしない。

Driveが戻ること、時計が進むこと、宣言したscopeだけをpumpすることが必要。
既知の進行停止をDriveがfalseで返せばProgressStoppedとなる。
戻らないcallbackや、進行すると宣言したのに停止した時計を、このwrapperが強制中断する機能はない。
リアルタイム性、preemption、WDTによる復帰を追加していない。

## Hostの証拠

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_wait_policy.py -o junit_family=xunit1
```

ASan／UBSan付き12件成功。既存plugin由来のexperimental warning12件。

- A/B/C×producer4種×進行機会3種×profile3種×参加2条件×同期／非同期の432条件。
  受理した値、経過時間、Driveとcallbackの回数、残るqueue、producer実行を独立modelへ照合。
- 即時tryの24条件でproducerと時計が進まないことを確認。
- 13個のliteral条件で禁止文脈・初期値あり・時計なし・進行不可・不正期限・0期限・一致／超過・停止を確認。
- 三方式それぞれ1,500生成条件、計4,500条件。時計wrap、文脈、参加、IRQ／相手コア能力、長い期限等を組み合わせる。
- 非同期pollのcallback利用、ISR拒否、wrap、完了後Idle、cancel後のデータ保持、不正再armを直接検査。

hostの非同期driverは観測窓を16進行までに固定する。時計やproducerが進めない場合のPendingはPendingのまま記録し、成功にしない。
これは製品の強制timeoutではない。

## ABIでのpolicy単体の参考値

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix wait \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-wait-link
```

6ケース×2 ABI×LTO有無、24構成がlink成功。
未参照wrapper関数と未使用AsyncWait objectの各4条件、計8行は追加Flash／静的RAM／init_arrayが0 byte、関連symbolなし。
RV32E・LTOなしの参考値。単位byte。

| policy入口 | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 30 | 24 | 0 | 0 |
| 即時predicate | 94 | 28 | 64 | 4 |
| 非同期状態保持 | 252 | 44 | 222 | 20 |
| 同期wrapper | 474 | 32 | 444 | 8 |

link入口はtoyのbool predicateと時計進行を使い、実queueやA/B/Cの実行器を含まない。
static RAMだけで同期wrapperが軽いと判断せず、呼出し中の通常stack、producer・同期・実時計を別途計上する。
RV32E・LTOなしの `.su` はblocking関数44 byte static、entry12 byte staticを報告した。
これらは関数frameで、実行器・IRQの重なりを含むstack peakではない。
この表をそのまま完成用途の待機APIの費用にはしない。
全行は [wait-size-matrix.csv](wait-size-matrix.csv)、入力・tool・ELF等は [wait-measurements.json](wait-measurements.json)。

host `work/exp03-wait-host-20261020/`、link `work/exp03-wait-link-20261020/` に入力と生記録を保存した。
移送archive `work/exp03-wait-20261020-evidence.tar.gz`、177,185 byte、
SHA-256 `ecc9f08cb67f65f868ee894641dcab9dad3644800b3c83d1997623827f31e0c9`。
command・終了値・stdout／stderr・map・symbol・disasm・section・.su・hashを照合した。

次はX0と実I/Oに同じ条件を接続し、待つ処理が保有するbus／buffer／lockとproducerの依存を含めて検査する。
producer能力の実検出、循環待ち、IRQ共有、実clock・stack、独立stack対照、最終的な公開範囲は残る。
