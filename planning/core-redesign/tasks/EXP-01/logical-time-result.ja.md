# EXP-01 論理時刻の伸長と不連続とtimer境界

2026-10-20。試作commit `7611e2c`。
[clockとcounterの入口](clock-time-result.ja.md)の後続として、時刻の伸長・端数・停止境界を純粋な算術模型で比較した。
製品core・USB・既存counterは変更していない。物理sampleの取得、実sleep、実clock精度、IRQ／core間共有は含まない。
方式の採用とArduinoの時間APIへの対応はP5に残す。

## 比較した時刻と前提

[入力と契約](../../logical-time-inputs.json)に6候補を固定した。
32 bit源の観測間隔、source epoch、固定周波数は呼出し側の宣言であり、このclassが測って保証するものではない。

| 候補 | 経過tickを認める条件 |
|---|---|
| low32-bounded | 最後に受理したsampleからの上限が既知と宣言され、2^32 tick未満。観測した差が上限以内 |
| low32-assumed | 上限が不明なら1周未満と仮定して進め、qualityへAssumedを残す |
| native64 | 同じsource epoch・rateの間にcounterがwrap／resetしない契約。後退したsampleは拒否 |
| six32-bounded／assumed | 6 MHz固定の同じ32 bit条件。1 µsを6 tickとして計算 |
| six64 | 6 MHz固定の64 bit条件 |

周回していない整数の差と32 bitの剰余値は、同じ情報ではない。
6 tickと2^32＋6 tickは同じ差を観測でき、上限6を誤って宣言してもclass側では見破れない。
推定方式も失われた周回を復元せず、仮定した結果を明示する。後の正常sampleだけでAssumedを消さない。
64 bitでも停止中にcounterが進まなければ、同じraw値から実時間が0秒か10秒かを判別できない。
native64はsleepを自動補完する機能ではない。

sampleはraw、source epoch、宣言Hzを持つ。source epochの不一致、同一epoch中のrate変更、不正な値、
上限超過、後退、曖昧な上限、算術overflowは受理しない。失敗時に出力Stampや内部の受理済みsampleを進めない。
診断用historyは最後に受理した値であり、現在時刻として返すnowではない。
全て直列操作で、周期IRQ、常設task、callback、delay内やloop外の自動処理は追加していない。

## 端数とrateの変更

論理値はuint64の整数µsで、同じrateの間は端数を保持する。
可変rateは分子／Hz、6 MHz固定は残りtick／6を使う。
1 tickを6回観測しても、個別に切捨てた0を足すのではなく、合計1 µsへ進む。
rate 3／7等は算術の感度分析であり、そのまま対象chipの物理clock候補とするものではない。

pause後のresume_knownは、最後の受理sampleからの経過が整数µsで既知という宣言を受ける。
同じrateなら端数を維持し、source epochを変えても論理epochを継続する。
rateが変わり端数が残る場合は、明示許可がなければPrecisionLossで拒否する。
許可した場合は捨てた分子／分母を報告し、qualityへRoundedを残す。
例えば3 Hzの1 tick後なら1／3 µsの端数があり、7 Hzへ変更する際の報告は分子1・分母3となる。
複数回の変更による損失が無条件に消えるとは扱わない。

経過が不明なresume_unknownは、新しい論理epochと指定anchorから始める。
旧期間の時間を補完せず、未知だった経過を既知の0として足す操作でもない。
qualityは新しいdomain内の条件として初期化する。source epochと論理epochは別の番号である。
論理epochの上限ではwrapせず、新domainを拒否する。同じdomainの既知経過での継続は可能。
番号の有効範囲は同じclock objectの寿命内で、別objectやbootを跨ぐglobal識別子ではない。

## timerへ渡す境界

DomainTimerは既存の[32 bit DeadlineTimer](../EXP-03/timer-preparation.ja.md)へ、受理されたStampとResultの組を渡す比較adapter。
32 bit deadlineを64 bit deadlineへ変えるものではない。delay／periodと関連する観測間隔は2^31 µs未満に制限する。
64 bit Stampの差で観測空白を確認し、半周以上の空白や後退では未来の予定を停止する。

論理epochが変わった場合も未来の予定を停止する。一方、既に保留していた通知は旧epochを付けてtakeできる。
新しいarmの受理で古い予定・保留通知を置き換える点は、既存timerの比較契約に合わせた。
未受理sampleはNoSourceとして進めず、推定・丸めを拒否する型と、明示的に許可する型を比較する。
quality拒否は時刻を消費せず、予定や既存通知をそのまま保持する。
通知をcallbackやmailboxへ自動配達しない。外側へ配達済みの通知の取消・宛先寿命は別の検査が必要。

## 正しさの検証

ASan／UBSan付きhost **146件**：論理時刻123、既存deadline20、workspace3が成功した。
conftestを追加せず、禁止ルールも維持した。
Python参照はFractionによる有理数と周回しない絶対期限を使い、C++の整数商・剰余実装とは分けた。

- 複数raw wrap、rateの異なる端数、既知／不明上限、推定の保持、出力保持。
- 既知／不明停止、rate変更時の明示損失、旧domain通知、半周、後退、集約回数の飽和。
- uint64最大の差、変換途中と累積値のoverflow、8 bit epochの255回退役。
- 6候補×8 seed×400観測の生成trace、計19,200観測と明示境界操作。
- 観測していない1周と、停止源の時間を復元できない負の例。

整数結果が一致したのは、宣言されたsourceと停止条件を使うこの模型の範囲である。
宣言周波数の実精度、観測上限の成立、外部のelapsed測定の精度、sleep／debug halt時の物理動作は未検証。

## ABIと整数幅の費用

V003／X035×LTO off／onで**52構成**をlinkした。
V003の宣言native源は32 bitなので、native64の2候補をそのpartの実用条件には含めていない。
X035では低32 bitとnative64を比較した。6 MHzは宣言した固定rateで、実測周波数ではない。
測定は全APIを呼ぶ入口、出力・観測変数、必要なlibgcc、memcpy／memsetを含む。Core全体や完成アプリの容量ではない。

| LTOあり | V003 image／Clock本体 | X035 image／Clock本体 |
|---|---:|---:|
| 可変rate・32 bit上限宣言 | 2,648／32 B | 1,992／32 B |
| 6 MHz固定・32 bit上限宣言 | 904／32 B | 704／32 B |
| 可変rate・native64 | 対象外 | 3,120／40 B |
| 6 MHz固定・native64 | 対象外 | 1,968／32 B |
| 可変rate＋厳格timer adapter | 3,280／32 B＋timer 40 B | 3,720／40 B＋timer 40 B |

Clock単独の静的RAM総量は上記Clock本体に入口等156 Bを含め、V003は188 B、X035 native可変は196 Bだった。
timerを加えた条件には、timer本体と通知観測用の変数も含む。未使用**20条件は追加image／静的RAM 0 B**で、初期化配列と動的確保のsymbolもなかった。
全52 ELFでload範囲・bin・全RAM sectionを照合した。
`.su`の最大単独frameはV003 228 B、X035 272 Bで、全APIを呼ぶ入口の値も含む。
libgcc内を含むcall-chain、IRQ、stack peakは未取得であり、既存の256 B stack予約で十分とは扱わない。

変更前52構成も保存し、同じ契約のまま入力幅と剰余の計算を特殊化した。
V003の6 MHz・32 bit・LTOありは**3,592→904 B**、可変rate・32 bitは**3,872→2,648 B**となり、Clock本体RAMは変わらなかった。
32 bit差を64 bit演算へ渡すこと、商と剰余で別の大きな補助コードが入ることが費用に影響した。
変更前後とも同じ有理数参照を通し、6候補の停止境界traceも同じ結果になった。
これはこの型・rate・compilerの最適化であり、全ての64 bit処理が不要だという結論ではない。

初回のLTO linkでは後から生成されたmemcpy参照に対する支援関数が残らず失敗した。
支援translation unitをLTO対象外にした診断と、成功した変更前r2／最終r3を区別して保存した。

## 記録と再実行

[最終52行](logical-time-size-matrix.csv)、[変更前後52行](logical-time-optimization.csv)、[入力・tool・trace・archive](logical-time-measurements.json)を保存した。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_logical_time.py tests/unit/core_redesign/test_deadline_timer.py \
  tests/unit/test_workspace.py -o junit_family=xunit1

python3 tests/platform/core_redesign/measure_logical_time.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-logical-time-recheck
```

native証拠の保存は`EXP01_TIME_EVIDENCE=新しい出力先`を指定する。出力先は上書きしない。
次は実counterへの接続、観測上限をどこで宣言・確認できるか、系列別sleepの保持条件、
複数timer・64 bit期限・tick／次期限方式、通知の寿命・配達と実時間／stackを比較する。
公開時間API、推定の許容、未知経過時の予定の扱いは、この単位では採用しない。
