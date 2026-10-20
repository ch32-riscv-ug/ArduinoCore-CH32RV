# EXP-03 資源を保持した待機と依存の観測

2026-10-20。入力作成時のHEAD `129ea12`。
製品コアには触れず、[既存の待機policy](wait-result.ja.md)へ資源を持つhost模型を接続した。
一般的なdeadlock detectorの採用、blocking APIの提供、待機中の自動unlockは決めていない。

## 実験の対象

待機元0、結果producer 1、補助producer 2／3と、bus・buffer・lockに対応する資源3個を置く。
資源はfree／ownerあり／所有者不明を区別する。actorは未獲得の必要資源と、完了を待つactorを持つ。
すべての列挙した依存が満たされないと進まない模型で、代替完了経路がある場合はこの表現をそのまま使えない。

producerは短いtryを行い、資源が空いていなければ何も獲得せず戻る。
補助actorは進めたときに自分の資源を返し、producer 1だけが結果Queueへ17を入れる。
Aは明示的に有限3処理を呼び、BはWorkExecutorの3宛先、CはEventExecutorの3宛先・集約通知を使う。
B/Cのbusy処理は次巡へ残り、同じ巡の中で無限に再実行しない。
IRQ／別コアproducerは独立刺激として直列に模擬する。実際の割り込み・並行共有の証拠にはしない。

| 操作 | 資源依存の扱い |
|---|---|
| 既存wait_for | 宣言されたproducer進行範囲を検査し、許可Driveを呼ぶ。資源依存は把握しない |
| 依存guard付きwait_for | 各Driveの前に模型の現在の所有・必要資源・依存を検査。把握できた停止理由または不明をProgressStoppedと別の診断値で返す |
| AsyncWaitの明示poll | poll自体はpumpしない。driverが外側の機会を与え、呼出し元の短い処理で資源を返す条件も分けて検査する |

同期待機中は待機元の通常コードを実行しない。非同期条件でだけ、指定した機会に呼出し元0が自分の資源を返す。
同期wrapperに同じ指定を渡しても、waiterを再開したことにはしない。
返却は安全に返せる模型の条件であり、実I/O中のbufferを直ちに返してよいという意味ではない。
取消、I/Oの実停止、buffer回収は別の契約で、このguardから自動実行しない。

## 依存を検査できる範囲

`WaitDependencies<N,R>` は最大16 actor／16資源の固定容量を使う比較用instrumentation。
処理は有限のbit集合の推移閉包で、heapや別stack、再帰dispatchを使わない。
必要な資源のownerとactorの完了依存から、結果producerに到達する依存だけを調べる。
無関係な循環や、同じactorへ別経路で合流するdiamondをdeadlockと誤認しない。

- 待機元0がreachable：待機元の通常処理へ戻らないと進めない既知の依存。
- 依存先が許可pumpに含まれない：例としてUでも未参加userが資源を持つ条件。
- 待機元以外の到達可能な循環：結果を作れない依存。非再帰lockを自分で再取得する条件も含む。
- 所有者または依存情報が不明：この比較guardでは進行を断定せず止める。
- 表現範囲外のmask／owner：不正入力として止める。
- NoKnownBlock：表現した情報に阻害がない。実時間、実backendや任意の利用者コードの進行保証ではない。

模型の所有台帳は実際のtryが参照するものと同じであり、この条件では追跡できる。
実backendで同じ事実が得られるかは別に確認が必要。
利用者の変数、外部libraryの内部待機、任意のcallbackの依存まで自動発見したものではない。
knownは完全な情報を供給したという呼出し側の宣言で、guardがその真偽を証明する機能はない。
owner不明を停止扱いにすると、実際には進める処理を拒否する場合もある。
情報不足だけのliteral条件では、既存wrapperは1 tickで完了するが、guardはUnknownでpump前に停止した。

台帳は各Driveの前に読み直す。最初に通過した後でwaiterへの依存が現れた場合も、次のpump前に停止する。
ただしここは直列のhost snapshotであり、実IRQやコア間で一貫したsnapshotを取得する同期費用は含めていない。

## Hostの検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_wait_dependencies.py \
  tests/unit/core_redesign/test_wait_policy.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

ASan／UBSan付き新規17件、既存wait policy12件、配置規則3件の計32件が成功。
既存plugin由来のexperimental warning32件。tests以下にconftest.pyは追加していない。

- literal20条件×操作3種×A/B/C＝180条件。bus／buffer／lockの直接保有、間接保有、循環、
  通常補助actorとuser参加、未知、初期値あり、0期限、不正期限、callback禁止等。
- 操作3種×機会3種×M/S/U×root producer4種×参加2条件×補助producer2種×参加2条件×A/B/C＝2592条件。
- 10 seedの各300生成条件を全3群へ与え、計9000条件。所有、依存、参加、clock wrap、
  非同期観測窓、途中の所有変更等を、独立Pythonの経路探索と逐次try模型へ照合。
- 最大16 actor／16資源、diamond、無関係なcycle、不正mask・ownerと初期化をnativeで直接検査。

代表例ではwaiterがbusを持ったまま待つと、既存wrapperは9回Drive後TimedOut、結果生成0、ownerは0のまま。
guard付きではDrive 0回、HeldByWaiterでProgressStoppedとなる。いずれも成功ではなく、資源を返していない。
非同期pollでも保持し続ければTimedOutだが、呼出し元が3 tick目に明示的に返す別条件では同じ3 tick目に完了する。
途中変更の条件では最初のpump中にbusがwaiter所有となり、guardは経過1、検査2回目で停止した。

同期wrapperはcallback／setup／ISRから禁止され、既存値を消費せず、guardやpumpも呼ばない。
非同期pollはcallbackでも使えるが、このdriverの外側の進行機会までpoll APIが呼んだことにしない。
非同期driverの観測窓は32機会であり、その時点のPendingを強制timeoutや完了へ書き換えない。

## ABIの費用

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix dependencies \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-dependencies-link
```

9ケース×2 ABI×LTO有無の36構成がlink成功。
未使用graph／guardの各4条件、計8行で追加Flash／静的RAM／init_arrayが0 byte、
baseline binaryと一致し、関連symbolも除去された。
全36行でRAM sectionの和と、実際の配置先addressから求めた占有spanが一致することも照合した。
全行は [dependency-size-matrix.csv](dependency-size-matrix.csv)、入力・tool・ELF等は [dependency-measurements.json](dependency-measurements.json)。
RV32E・LTOなしの参考値。単位byte。

| 条件 | Flash image | 静的RAM |
|---|---:|---:|
| baseline | 30 | 24 |
| 依存検査だけ・2 actor／1資源 | 706 | 60 |
| 依存検査だけ・4 actor／3資源 | 854 | 92 |
| 依存検査だけ・8 actor／3資源 | 1070 | 156 |
| 依存検査だけ・16 actor／16資源 | 1186 | 296 |
| toyのwaitだけ | 450 | 32 |
| toyのguard付きwait・4 actor／3資源 | 1398 | 100 |

このtoy入口ではguard結合がplain waitよりFlash 948、静的RAM 68 byte増えた。
完全な依存台帳が無償で得られるわけではなく、容量と機能の条件を明示して比較する。
同じ条件の `.su` は検査単体のinspectが60、guard結合中のinspectが84、blockingが52 byte staticと報告した。
同じ関数でもコンパイラが呼出し条件で変えるため、関数名だけで全構成共通のstack量としない。


link入口のDriveはtoyのbool predicateと時計であり、A/B/Cのdispatcher、実Queue、実資源台帳の取得は含まない。
単純なwait、依存検査単体、guard結合の境界を分けて測る。
guard側には台帳設定と診断観測の入口も含むため、差分をguard関数だけの機械語サイズとはしない。
各行の `.su` は関数frameであって、呼出し・IRQ・別コアを含むstack peakではない。
actorごとの必要資源・完了依存・producer種別・参加・knownとowner配列を保持する。
4 actor／3資源の台帳はABI上68 byte。動的初期化を避け、未使用除去を別に検査した。

host `work/exp03-dependencies-host-20261020-r2/`、link `work/exp03-dependencies-link-20261020/`、配置監査 `work/exp03-dependencies-audit-20261020/` に入力と生記録を保存した。
コマンド・終了値・stdout／stderr・JUnit・ELF・map・symbol・section・逆アセンブル・.suとhashを照合した。
移送archive `work/exp03-dependencies-20261020-evidence.tar.gz`、517,616 byte、
SHA-256 `9278bfa01839c33a9bf4ee31aa1c88a71c3db4ff15ccbc6fae87b9a1877152bd`。出力先は毎回未作成のものを指定する。
入力HEADだけでなく、当時の試作を含むhash付き入力を再現の根拠にする。

## 後続

blocking APIの提供範囲や、未知の依存を拒否する方式の採否はP5へ残す。
台帳を全用途に常設する提案ではなく、依存を明示する場合の費用と、未知のものを扱えない境界を得た。
次は完了枠予約・arbiter、実I/Oと資源の依存、IRQ共有、X0のcompile時除去・変更課題を進める。
実clock、実資源の可視性、I/Oの取消・停止、任意のcallbackから戻らない条件は未検証。
