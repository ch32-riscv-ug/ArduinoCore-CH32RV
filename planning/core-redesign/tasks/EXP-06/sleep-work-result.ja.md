# N06 休止・分割作業・I/O回収の接続

2026-10-22。実験実装 `c88828f`。N06の休止側の追加検証で、方式の採用・N06全体の終了ではない。
[入力](../../sleep-work-inputs.json)、[測定manifest](sleep-work-measurements.json)、
[ABI表](sleep-work-size-matrix.csv)、[変更課題](sleep-work-exercise.ja.md)。
既存のCoordinator／Port／LogicalClock／WorkExecutor／EventExecutorは変更せず、実験adapterを追加した。
production core／USB／Network／H417の並行実装は変更していない。

## 接続した責務

[休止flow](v006-sleep-flow-result.ja.md)へ、16 B I/O bufferを持つworker2個を接続した。
各workerは1〜255 stepの有限jobを1件受理し、1回の通常callbackでI/O終端を1件回収するか、jobを1 step進める。
I/Oが終端になっていなければ、bufferへ触れないjobのstepを進められる。
PrepareでI/Oとjobの新規受理を閉じ、I/O取消を要求する。取消受理だけではbufferは解放しない。
明示的なquiescent終端の後に通常workerがtakeし、jobも全step終了してからQuiet／Offへ進む。
request／poll／enter／clock復元／timer操作から通常callbackを呼ばない。

R／Aはこの接続条件では同じ直接進行処理を使う。Rの裸のハード制御とA全体の費用比較ではない。
BはworkerごとにWorkExecutor1枠、C-notification-bridgeはworkerごとにEventExecutor1宛先を持ち、集約hintを使う。
Cのpayload配送やglobal共有executorの比較ではない。
ヒントの発行も明示的な通常文脈で行い、実ISRからpostする共有方式を実装しない。
参加者一覧はExplicit2個／Registry容量2／4を比較。2 workerとも登録し、registryの余り容量を表示へ隠さない。
登録漏れを自動検出できるという主張は、既存flow結果と同様にしない。

M／S／U、Explicit／AfterLoop／Delay、System／User opt-out／User opt-inを元の規則どおり適用する。
例えばSでUser jobをdelayから進めなければ、そのjobを残した休止準備も進まない。
有限jobを強制実行する優先度や、休止準備時だけUserをSystem扱いする特例は入れない。

## 復帰・時刻・中断

workerは論理clockがRunningで、そのportがpoweredのときだけ進行できる。
clock復元前にReadyを宣言してもportのRestoreは進まない。片側だけRestoreが済んでも新規受理は再開しない。
両側Restore→Activate成功後に一覧のfreezeを解除する。
休止準備をabortすると新規受理を戻すが、要求済みのI/O取消・未回収lease・残jobは保持する。
残jobを勝手に捨てたり、cancelを未要求へ巻き戻したりしない。

clockは固定の6 MHz sampleと観測間隔をcallerが宣言する算術模型。
known復帰は同一domainの経過時間へ加算し、unknown復帰は新domainを開始する。
Timer有効時は、その違いによる期限通知／DomainChangedと、通知保持中のrequest拒否を検査する。
Armed後に明示timer_pollで通知がpendingになった場合もenterが準備を取りやめる。
タイマーを自動pollしたり、未来の期限に合わせて実wake sourceを設定したりはしない。
`wake_armed`／atomic gateは既存Coordinatorへ渡す宣言であり、V006上で成立する命令列ではない。

Timer無効時は空のbaseへ特殊化し、Timer object・pointer・placeholder memberを持たない。
既存Coordinator側のNoDeadline特殊化も使う。clock依存は残る。

## 検査結果

最終ASan／UBSan runはnative104件＋workspace3件、失敗・skipなし。
588 trace・109,628コマンド、4候補を含む147組で全コマンド後の出力が一致した。
うち45組は3種類の一覧×4候補の全12構成も一致した。

- jobの有限drain、新規受理停止、I/Oのquiescent witness拒否／受理、終端保持と通常workerによるtake。
- M／S／Uとdomain／opt-inによる進行差、Setup／ISR禁止とCallback文脈の再入拒否。
- clock復元前・片側復元後の停止、Restore失敗とretry、known／unknown時刻と期限。
- BeforeCheck／AfterOff／AfterWaitの入力、abort時のlive I/Oと残job、Armed後の期限通知。
- 10 seed×100 lifecycleで両portの取消・回収・job・休止・復帰を接続し、Pythonで独立に累積step／回収件数を計算して照合。

独立計数はjob stepと回収件数の証拠であり、全sleep stateの独立emulatorではない。
4候補の一致だけで正しさを確定せず、上記の状態／所有／受付条件を個別に検査した。
これはP1の全用途乱数予算を満たした宣言ではない。

負の比較として、jobを休止判定から除外する実験templateも動かした。
I/OだけがQuietなら、3 step残したworkerでもOffへ進める反例を保存した。
通常のCheckJobs=trueではQuiescingに留まる。負の比較は公開modeではない。

## ターゲット向け費用

V003 RV32E／X035 RV32I、LTOなし／あり、4候補×一覧3種類×Timer有無×使用／未使用とempty、計196 buildが成功。
使用96行、未使用96行、empty4行。未使用96行は各emptyと同じimage・静的RAM0 B。
allocator・constructor arrayはない。Timer無効を空baseへ変更する前後のhost traceは全て同じだった。
V006固有のlinker／startup／wake backendを使った測定ではなく、2つの既存ABI profileでの構造的費用である。

以下はExplicit、System、V003、LTOあり。harness132 Bを静的RAMに含む。
アプリobjectには2 workerの観測用counter24 B等も含む。製品の最小費用や実装済みcoreの総費用ではない。

| 候補 | Timer | image B | 静的RAM B | アプリobject B | 最大単一.su B |
|---|---|---:|---:|---:|---:|
| R／A | 無効 | 3,064 | 420 | 288 | 40 |
| R／A | 有効 | 3,592 | 468 | 336 | 52 |
| B-work | 無効 | 3,368 | 484 | 352 | 52 |
| B-work | 有効 | 4,008 | 532 | 400 | 60 |
| C-notification-bridge | 無効 | 3,408 | 508 | 376 | 60 |
| C-notification-bridge | 有効 | 3,928 | 556 | 424 | 60 |

R／Timer無効の一覧別ではExplicitのimage3,064 B／RAM420 Bに対し、Registry2は3,168 B／436 B、
Registry4は3,200 B／460 B。利用数2個でも、確保した登録枠の費用は残る。
Timer無効のempty-base化では、この測定のアプリobjectが8 B小さくなった。配置・整列を含む差で、空typeのsizeofが8 Bという意味ではない。
単一.suはcall chainやIRQを含むpeak stackではない。static RAMとimageがlink範囲内でも、全runtimeのfitを証明しない。

## 再現と履歴

最終hostは `work/exp06-sleep-work-host-20261022-r4`、XML／logは `work/exp06-sleep-work-tests-20261022-r4.*`。
最終ABIは `work/exp06-sleep-work-abi-20261022-r2`。
`work/exp06-sleep-work-analysis-20261022` に集計script、hash検査、旧新比較、ABI表を保存した。
コマンド・compiler／入力hash・binary・trace・linker・ELF・逆アセンブルをarchiveへ含めた。

初回hostは同一行のif列が-Werror=misleading-indentationで失敗し、native95件setup error／workspace3件成功。
driverの整形修正後r2で98件成功。abort／直前期限の9件を追加したr3で107件成功。
Timer storageのempty-base化後r4も107件成功し、588 traceはr3とbyte一致。
ABIは変更前196件、最終196件とも成功。変更前の値は履歴として保存し、最終値と混ぜない。

## 残る境界

V006実adapter・実I/O／IRQ・AWU／clock復帰・wake競合・電流・実peakは未検証。
実runtime／全配置・乱数予算の横断補完はN08、実fixtureと観測はN09へ残す。
H417のmessage公開→通知→通常実行→返却、片側停止と世代更新の実行器接続はN06の残作業。
本結果でN06全体を終了せず、次にその独立した接続を進める。公開境界と系列構成の採否はP5で扱う。
