# 休止準備・I/O回収・時刻・参加者の結合

2026-10-20。[資料と待機境界](v006-sleep-result.ja.md)の次のhost／ABI単位。

追試の[timer依存除去と段階失敗](v006-sleep-features-result.ja.md)では同じソースのtimerあり／なしを比較し、clock guardと失敗時の状態を追加検査した。
本ページの数値はcommit `2a56a1d`の記録であり、更新後のソースの費用は追試を参照する。
コア本体・USB・実WFI／WFEは変更せず、実行方式や公開APIを採用するものではない。
[入力](../../sleep-flow-inputs.json)は明示一覧2個、任意registry容量2／4の3条件。

## 同じ用途と契約

通常の仮想時刻10秒後に、表示を想定した32 byte転送中に休止を要求する。
第2の有限I/Oは8 byte要求を持ち、取消と失敗・部分生成の回収も比較する。
両者は既存`IoSlot<32>`のfake adapterであり、第2のI/Oを実Flash保存の実装とみなさない。
clockは既存32 bit論理clock、timerは既存domain付き期限部品を結合した。
6 MHzは共通のfake入力値で、V006の物理clockやStandby中の動作を宣言するものではない。

| 状態 | 進む条件・利用者側の操作 |
|---|---|
| Active | 新規I/Oを受理できる。inputまたはtimer通知がpendingなら休止要求を拒否 |
| Quiescing | 参加I/Oの新規受理を閉じ、実行中ならcancelを要求。利用側が終端をtakeするまで待つ |
| Armed | 全参加bufferの明示回収が済んだ。wake設定とatomicな入口の能力宣言を確認する |
| Sleeping | clockをpauseし、参加I/Oを閉じたfake状態。入力を保持する入口を仮定する |
| Recovering | input／timer／spurious wakeで復帰。clock再開と、すべての参加I/Oの再設定を待つ |
| Fault | 再設定失敗等を保持し、新規受理を閉じたままにする。原因の修復と明示retryでRecoveringへ戻る |

request／pollはI/O終端を自動takeせず、ユーザーcallbackを暗黙に実行しない。
cancel受理だけ、またはhardware終端だけではbufferは空かない。利用側がtakeして初めて休止を進められる。
完了通知が残る間も明示回収でき、終了結果・転送長・生成dataを確認する。

準備中の新入力や期限通知は準備を中止する。取消要求は取り消さず、元の転送が遅れて成功することも許す。
bufferはその成功／失敗／取消の終端回収まで保持する。
復帰では一部のI/Oが先に再設定できても、新規受理は全参加者が完了するまで開かない。
途中の失敗でFaultとなっても、この閉鎖を保つ。clock再開の失敗・overflowも受理再開の根拠にならない。

## 待機境界と時刻

入口前のinputは休止を中止し、clockは走行中のまま戻る。
power-off後または待機後のinputはstickyな条件として残し、Recoveringへ進む。
後者2刺激は同じfake gateの復帰経路を使う。実命令間の異なる窓を検証したものではない。
`wake_armed`と`atomic_gate_supported`はadapterの宣言であり、このC++が実IRQ mask・event保持を実装しているわけではない。
宣言がない場合は実行せずArmedを保ち、明示abortで通常処理へ戻れる。
[前の304順序](v006-sleep-result.ja.md#休止直前の通知を304通りで比較)が示す物理条件は未検証のままである。

clockがpause中は、過去の有効stampだけを使ってtimerを進めない。
clockの状態を参照し、timerのarm／pollへPausedを渡す接続を含めた。
復帰後のelapsedが独立した根拠で既知か、unknownかを明示する。

| fake復帰条件 | 再開stamp（µs／domain） | 1秒周期timerの明示poll |
|---|---|---|
| 既知2.5秒 | 12,500,000／1 | latest due 12,000,000、skipped 1の通知を1個保持 |
| 経過不明、anchor 0 | 0／2 | domain変更で将来の旧scheduleを止める。経過や回数を創作しない |

起床原因だけから「設定したAWU時間が経過した」と判断しない。
この2.5秒は試験入力の宣言であり、AWU・RTC・V006実機から測定した値ではない。
旧domainのscheduleを再設定するか、外部時刻へ照合するかは用途側の明示的な処理に残す。

## 明示一覧と任意registry

明示一覧は2つの具体的な参加型へ直接呼び出す。registryは固定長のcontext／function pointer／ticketを持つ。
同じ2 I/Oを初期登録し、容量4では空き2枠を持つ。registryの追加／解除・重複・満杯・古いticketも検査する。
準備から復帰完了まで一覧をfreezeし、callback先の解除や差替えを拒否する。
実行中I/Oがある参加者の解除も拒否する。32 bit ticketは再利用せず、8 bit試験型で上限を検査した。
休止attemptにも別のticketを付け、以前のenter／wake／restoreを新しいattemptへ作用させない。

これは同じobject寿命内の識別であり、別registryのtoken取り違えを防ぐ所有者IDではない。
一覧・clock・timer・参加contextはcoordinatorの貸出中と復帰まで生存することが前提。
callbackは有限のadapter操作だけを行い、同じcoordinatorへ再入しない。通常文脈の直列模型であり、ISRから直接呼ぶ手順書ではない。

**未登録のI/Oは検出できない。** 登録を外したI/Oが転送中でも、残りだけで休止へ進める反例を記録した。
これは実機で安全な使い方として許可した意味ではなく、一覧が完全であることをこのmanagerだけでは証明できないという限界。
明示一覧でも、用途が必要な資源を列挙した根拠が必要になる。

## Hostの証拠

新規64件、前の待機境界54件、workspace規則3件、合計121件が通過。
新規C++はASan／UBSanと警告errorで検査した。tests配下にconftest.pyは置かない。

- 10秒の通常処理、両I/Oの取消・停止宣言・明示回収、部分結果とbuffer保護。
- 最終確認前後のinput保持、wake／入口能力の不足、期限通知による休止中止。
- 既知／不明の復帰、epoch・期限、経過overflow、部分復帰・失敗・retry。
- registryの凍結・解除・容量・ticket上限、attempt上限、未登録資源の反例。
- 8 seed×500操作で明示一覧とregistry2／4へ同じ刺激を与え、登録token以外の観測が一致。

加えて、保持したnative binaryで既知／不明復帰の6 traceと失敗retryの3 traceを再生し、入力・全出力・hashを保存した。
既知／不明の各条件で3方式の全観測を照合した。random traceがすべての状態遷移を網羅したという主張ではない。

## ABI費用

既存のV003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、GCC 14.3.0、`-Os`、LTO off／on。
V006用途の純粋な構造を小型／通常ABIで比較したもので、V006のMMIO・起動・命令拡張・物理clockを接続したELFではない。
empty・3方式・I/Oだけ・clockとtimerだけ・未使用3方式の計36 ELFをlinkした。

| 所有状態の構成 | 明示2個 B | registry容量2 B | registry容量4 B |
|---|---:|---:|---:|
| 32 byte buffer付きPort 2個 | 144 | 144 | 144 |
| 論理clock＋timer | 32＋40 | 32＋40 | 32＋40 |
| 参加者一覧 | 12 | 32 | 56 |
| coordinator | 20 | 20 | 20 |
| 上記object合計 | 248 | 268 | 292 |

| 構成 | V003 image B off／on | X035 image B off／on |
|---|---:|---:|
| empty | 8／8 | 8／8 |
| 明示2個の結合 | 3,248／3,240 | 2,896／2,920 |
| registry容量2の結合 | 3,592／3,448 | 3,152／3,168 |
| registry容量4の結合 | 3,624／3,488 | 3,176／3,208 |
| I/Oだけ | 992／984 | 800／792 |
| clock＋timerだけ | 1,256／1,256 | 1,016／1,016 |

全imageはvolatile引数の検査入口と必要なsupportを含む。明示一覧とregistryの機能差に、追加／解除・凍結・ticketを含む。
I/Oだけ、clock＋timerだけには休止調停の能力がなく、結合の代替品として順位付けしない。
二つの対照のimageを単純加算／減算してcoordinator単体の費用ともしない。inline・共有処理・harnessが異なる。

結合時のharness objectは明示174 B、registry182 B。padding込みの静的RAMは明示432／440 B（off／on）、registry2が464 B、registry4が488 B。
一覧のRAM差20／44 Bと、検査用token等の差を混同しない。
clockとtimerの72 Bは**今回使う能力**の費用であり、timerなしの用途へも必須とする設計を採用したものではない。
個々の機能を除く構成は別条件として比較する。

未使用12条件はemptyとimage hashが一致し、追加Flash／RAM 0 Bだった。
constructor配列・allocator symbolがないことを検査した。
結合ケースで出力された最大単一frameはV003 48–52 B、X035 80–96 B。
I/O／clock単独の入口とはinline状態も異なる。呼出し連鎖・IRQ込みのstack peak、実行時間、禁止時間の証拠ではない。

## 残る作業

物理adapterの停止・再初期化、wake設定、最終確認と待機の競合、元のIRQ状態と可視性、V006での実起動・復帰・電流は未検証。
callbackの登録寿命を実driverの所有へ結ぶこと、準備／復帰の各段階のdriver失敗、全参加者の根拠と公平性も残す。
今回のPort失敗刺激は復帰のWaiting／Ready／Failedを宣言するもので、実bus障害注入ではない。
登録方式をsystemだけにするかuserへ公開するか、明示処理をどこまでまとめるかの採否はP5で判断する。

[全ABI行](v006-sleep-flow-size-matrix.csv)、[再現記録](v006-sleep-flow-measurements.json)、
[段階を分けて処理する課題](v006-sleep-flow-exercise.ja.md)を参照する。
