# 休止のtimer依存除去と段階失敗

2026-10-20。[I/O・時刻・一覧の結合](v006-sleep-flow-result.ja.md)に対する追試。

次の[生きたleaseとの複合検査](v006-sleep-lease-fault-result.ja.md)で、Prepare／Abort失敗と片方・両方の転送中432条件を追加した。
本ページの156条件は、準備開始時に転送を持たない段階失敗の記録として区別する。
実験ソースだけを変更し、コア本体・USB・実機の待機命令は変更していない。
公開APIや実行方式の採用はP5へ残す。[入力](../../sleep-features-inputs.json)で比較条件を固定した。

## timerを使わない場合

coordinatorの期限依存を型で選び、`NoDeadline`ではtimer objectも参照pointerも保持しない。
この経路のbindにdummy timerは不要。input、I/Oの所有と回収、clockのpause／既知・不明再開は同じまま残す。
通常のtemplate引数は従来どおりtimer付きなので、既存の結合刺激をそのまま検査できる。

timerなしでは期限通知による休止中止を提供しない。harnessのarm／poll／take命令は「機能なし」を返し、成功や単なる0件とは区別する。
入力等のwake sourceの設定と入口の能力宣言は引き続き必要であり、任意時刻の自動復帰が追加されたわけではない。
clockまで消す構成、実AWU／RTCの接続、timerなしでも期限が欲しい用途の意味は別の条件になる。

## 失敗時の状態を追加検査

fake adapterが通常の有限操作を実行してから、Waiting／Failedを返す刺激を入れた。
1個目／2個目の参加者、明示一覧／registry2／4、timerあり／なしで同じ条件を使う。
callback内で別のcoordinator操作や新規I/Oを開始しない直列契約。Prepareは失敗を返す場合でも受理を閉じることが前提になる。

| 刺激する操作 | Waitingを返した後 | Failedを返した後 |
|---|---|---|
| Prepare | Quiescingで再確認を待つ | Fault |
| Quiet（入口の最終確認） | Quiescingへ戻る | Fault |
| Off | Fault | Fault |
| Restore | Recoveringで再確認を待つ | Fault |
| Activate | Fault、再度Prepareして受理を閉じる | 同左 |
| Abort | Fault、再度Prepareして受理を閉じる | 同左 |

Off等の途中で副作用があった可能性があるため、非Readyをすべて「後でもう一度入口へ行けばよい」とは扱わない。
表は関数が戻った境界の状態であり、途中の副作用を原子操作として巻き戻せた意味ではない。
特に途中でI/Oを開始するcallbackやISR共有を許した場合まで、この閉鎖を保証したものではない。

QuietでFailedを返す場合を単なるbusyと区別し、Faultとして保持するようにした。
またQuiet確認の中でclockをpauseさせる刺激を追加した。
coordinatorが以前持っていた「再開済み」bitがtrueでも、論理clockが現在Runningでなければclock-readyとは扱わない。
新規休止要求を拒否し、復帰retry中もI/Oを開かず、clockを明示再開してから参加者を再開する。
これは論理clockの状態確認であり、物理発振・周波数・RCCの自動監視ではない。

## Hostの証拠

timerなしの追加18件、段階失敗156件、既存結合64件、workspace規則3件、合計241件が通過した。
C++はASan／UBSanと警告errorを使用。tests配下にconftest.pyを置かない。

失敗156条件は6段階×2返値＋clock喪失1種を、2参加位置×2期限構成×3一覧方式で検査した。
clock喪失ではFault→retry後もRecoveringと受理閉鎖を保ち、明示clock再開後だけActiveへ戻る。
Abortに使ったinputは修復後も明示takeまで保持された。

timerが発火しない同一刺激でtimerあり／なしの観測を比較し、登録情報を含むI/O・input・clock・phaseが一致した。
既知／不明の再開、入口後のinput、ticket上限、timerなし命令の拒否も含む。
保持したnative binaryから156故障traceと24結合traceの全入力・出力・hashを保存した。

この故障fixtureは休止準備開始時にI/O転送を持たない。転送中の取消・明示回収は既存64件で検査するが、
その全タイミングと全故障を組み合わせた検査ではない。実bus故障、timeout、回収不能、IRQ／core間共有は未検証。
callback呼出し回数の有限性と戻り時の状態を確認し、CPU時間やcallbackの実時間上限を測定したわけではない。

## 同一ソースでのABI比較

GCC 14.3.0、`-Os`、V003／X035の既存profile、LTO off／on。
V006のMMIO・startupを接続した実機ELFではなく、従来と同じ純粋な構造の比較。
empty、3一覧方式×timerあり／なし、I/Oだけ・clock＋timerだけ・clockだけ、未使用6条件の計64 ELFを測った。
今回のclock guard等の修正を**両方へ同じように適用した現在のソース**で差を取る。
前のcommitのimageから引いた差をtimerの費用とはしない。

| 利用するobject | timerあり B | timerなし B |
|---|---:|---:|
| Port 2個 | 144 | 144 |
| 論理clock | 32 | 32 |
| timer | 40 | 0 |
| coordinator | 20 | 16 |
| 明示一覧 | 12 | 12 |
| registry2／4 | 32／56 | 32／56 |

各方式で利用側のobjectは44 B減った。timerなしのcoordinatorに空きpointerを残していない。
timerなし28行（未使用やclock単独も含む）で、timer objectとDomainTimer／DeadlineTimerのsymbolが存在しないことを確認した。
未使用24条件はemptyとimage hashが一致し、追加Flash／RAM 0 Bだった。
constructor配列・allocator symbolがないことも検査した。

| 構成 | V003 LTO on image B：あり→なし | X035 LTO on image B：あり→なし |
|---|---:|---:|
| 明示2個 | 3,304→2,800（504減） | 3,008→2,576（432減） |
| registry2 | 3,504→3,096（408減） | 3,272→2,816（456減） |
| registry4 | 3,544→3,136（408減） | 3,304→2,848（456減） |

| 静的RAM（両profile共通） | timerあり off／on B | timerなし off／on B |
|---|---:|---:|
| 明示2個 | 432／440 | 376／372 |
| registry2 | 464／464 | 408／404 |
| registry4 | 488／488 | 432／428 |

この静的RAMにはharnessとpaddingも含む。timerを使わない検査入口ではnotice用16 Bも消えている。
したがって静的RAM差56–68 Bをlibrary単体の除去量として扱わない。利用するobjectの差は44 Bである。
imageも全検査入口・必要なsupport込みであり、個々の関数の費用へ分解したものではない。
timerなしは能力が異なるので、期限通知が必要な用途の同等代替品として小さいと結論しない。

結合ケースの出力された最大単一frameはtimerありでV003 48–56 B・X035 96 B、timerなしでV003 36–44 B・X035 80 B。
呼出し連鎖やIRQ込みのstack peak、禁止時間、実機速度・電流の証拠ではない。
[全64行](v006-sleep-features-size-matrix.csv)と[再現記録](v006-sleep-features-measurements.json)を参照する。

## 残る境界

参加者一覧の完全性、別instanceのtoken識別、能動I/Oと複合故障、失敗を返す前のadapter側の受理閉鎖、非再入・ISR共有は別の契約になる。
実driverの停止・clock／I/O再設定・wake設定・最終確認と待機・option byte・復帰・電流は引き続き実機未検証。
NoDeadlineをsystem構成として選ぶかuserへ公開するか、さらに何を型で外すかの採否はP5へ渡す。
[能力を外す課題](v006-sleep-features-exercise.ja.md)は利用者による評価前である。
