# 資源管理付きI2Cと完了callbackの接続

2026-10-21。実装commit `d818622`。
[入力](../../i2c-event-inputs.json)、[全28 ELF](i2c-event-size-matrix.csv)、
[host・tool・hash・比較台帳](i2c-event-measurements.json)を保存した。
[資源管理付きI2C](../EXP-01/i2c-managed-handoff-result.ja.md)へ、既存の[完了配信glue](completion-delivery-result.ja.md)とEventExecutorを接続した。
既存headerを変更せず、実験driver・独立link用entryだけを追加した。製品コア・USB・Networkは編集していない。
これは直列に呼ぶhost／ABI実験で、callbackの公開範囲・自動進行・通知形式の採用決定ではない。

## 接続した範囲

同じDomainを使うManagerを2個置き、Managerごとに固定のcallback contextを登録した。
EventExecutorは宛先2個、それぞれ容量1のFIFOを持つ。共有の容量1 FIFOではない。
messageは従来のrequest tokenだけで、byte列・結果metadata・発生元instanceは含まない。
callback contextが回収先Managerを指定し、callbackはnoticeを再確認してそのManagerのtakeを呼ぶ。
受信bufferはコピーせず、callbackによる回収後に利用者が元bufferを読めるようになる。

| 明示操作 | 周辺・資源 | 完了record・buffer | callback |
|---|---|---|---|
| Master終端、reap前 | 保持中 | 外側はRunning | pumpはNotPending |
| reap | backend借用を回収。まだopen | Terminal結果と借用を保持 | 自動実行しない |
| close_backend | 他のQueued／Runningがなければ周辺とleaseを返却 | 保持したまま | 自動実行しない |
| pump | MMIOなし、lease不要 | pendingを残す | FIFOへtokenを置くだけ |
| turnでcallback配達 | MMIOなし、lease不要 | notice再確認後のtakeで回収 | 当該Managerの結果を処理 |
| 満杯／古い宛先 | 変更しない | pending・結果・借用を保持 | 明示retryを待つ |

clock宣言変更や別Managerの通信中にも、旧Managerのcallback回収は可能。
別ownerのleaseを返すことも、周辺へstep／dispatchすることもない。
一方、leaseを返しただけではcallbackやbuffer回収は進まない。
配達機会を与えなければbufferとrecordは保持され、受理容量を使い続ける。
時間上限やcallbackによる暗黙の次要求起動は設けていない。

M/S/U、System／User／opted-in User、Explicit／AfterLoop／Delayは既存候補のまま比較する。
AfterLoop／Delay相当の機会も実験側が明示turnで与える。
実際のArduino loop外・delay内へhookを追加したものではない。

## 発生元が異なるtokenの反例

request tokenのgeneration・record index・clientはManager内での識別子。
別Managerなら両方の最初の要求が `(1,0,0)` になり得る。
AのtokenをBに固定したcallbackの宛先へ意図的にpumpすると、B側のnotice再確認はB自身の要求に成功し、Bの結果を回収してしまう。
Aの結果・bufferは保持されたままで、Aの配達が成功したわけではない。

この反例は、正しい宛先へsendした後の世代再検証の失敗ではない。
現在のglueは任意の有効targetを受け取るため、発生元に対応するtargetを呼出し元が選ぶ、という接続条件が必要になる。
実験の通常経路ではcontextとManagerの対応を固定する。誤配信を安全にする拡張は加えていない。

発生元を含むenvelope、発生元ごとの型／宛先、登録時の対応表、共有宛先への転送adapterなどはP5で比較・判断する余地がある。
request generationだけを増やしても、Managerが違うことの証明にはならない。
object再生成・bootを跨ぐ識別や、callback contextを途中で別objectへ付け替える使い方も今回の保証外。

## Hostの証拠

ASan／UBSan、`-Wall -Wextra -Werror`でnative128件＋workspace3件、計131件が通過した。
配信128 trace・5,358操作、直接回収の基準34 trace・2,724操作を保存した。
専用3 record／共有2 record、Managerあたり利用者3、全体で外側buffer64 B×3を使う。
通常経路と意図的な誤配信の反例の双方で、最後には全bufferを回収した。

- write・read長1／2／3／4／8／32／64・tx＋readを両方式で比較した34条件で、全MMIO・register・送信列・ADDR時control・byteと資源台帳状態が直接回収と一致した。callbackのCompletionも直接takeと一致した。
- 終端だけではpumpがNotPending、reap後はAcceptedでもbufferを保持、turn後に回収という境界。
- 両方式×3 domain×3 profile×3機会の54条件。周辺closeとPCLK宣言変更後も、許可されたcallbackだけが実行される。
- 同じ宛先FIFOの満杯中でも別のI2C要求を完了できる。旧結果配達後に、保持中の次結果を明示retryできる。
- callbackを待たず直接takeしrecordを再利用した場合、旧hintは新結果を回収しない。拒否時のcallback出力も保持する。
- client endや明示ack後のhintは回収を行わない。結果は消えず、明示takeでcleanupする。
- 宛先解除でqueued hintを捨ててもpending保持なら再登録後にretryできる。enqueue時にcallerがackした反例では通知根拠が失われる。
- Aのhintを待たせたまま資源をBへ渡し、Bの通信中にAのcallbackを実行しても、Bのlease・borrow・周辺状態は変わらない。その後Bも自分の固定callbackで回収する。
- A／Bの同値tokenをBへ誤配信した反例と、正しい宛先へのretryによるAのcleanup。
- callbackからのturn再入、未初期化・Setup・Callback・ISR指定のturn拒否。拒否中はhintとbufferを保持する。
- 5 error flagの失敗prefixと、Queued取消／client endを配信へ接続した結果。Queued取消のbackend generationは0のまま。

各操作後のCheckedBusと状態assertに加え、34条件は既存の資源管理付きdriverとの差分で確認した。
独立した全状態機械や実IRQとの照合ではない。
元のMaster・Fake・Domain・Manager・Managed・配信glue・EventExecutorなどのhashは以前の記録と一致する。
fixtureはmodule内に置き、conftest.pyは追加していない。

## 費用

同じ観測harnessで、2 Managerの直接回収と、2宛先・各FIFO容量1を追加した回収を比較した。
V003 rv32ec／ilp32e、X035 rv32imac／ilp32、LTO off／onで28 ELFをlinkした。
共通部分は2 Manager、Domain、Pins、buffer3本、直接回収の操作・観測変数を含む。
配信ありのimageにも直接回収経路を残す。
LTOありの値は次のとおり。

| 対象 | record方式 | 直接image B | 配信image B | 差 B | 直接RAM B | 配信RAM B | 差 B |
|---|---|---:|---:|---:|---:|---:|---:|
| V003 | 各専用3 | 4,080 | 5,080 | 1,000 | 688 | 784 | 96 |
| V003 | 各共有2 | 4,072 | 5,112 | 1,040 | 616 | 712 | 96 |
| X035 | 各専用3 | 3,744 | 4,688 | 944 | 688 | 784 | 96 |
| X035 | 各共有2 | 3,776 | 4,688 | 912 | 616 | 712 | 96 |

追加objectはEventExecutor 80 Bと宛先token2個16 B、計96 B。全8比較でRAM差96 Bとなった。
Manager2個は専用312 B／共有240 B、Domain40 B、Pins1 B、buffer204 B、共通観測変数のsymbol合計127 B。
Flash差にはbind／remove／pump／turnの観測操作も含む。実アプリの最小配信費用とは断定しない。
V003／X035で型layoutが同じでも生成命令・ABIが違うため、Flash差は同一でない。

全28構成の.dataは0 B。未使用8構成では全object・RAMの追加が0 Bで、空の8 B imageとhashも一致した。
allocator・constructor arrayは検出していない。
LTOありの最大単体frameは直接回収がV003 36 B／X035 80 B、配信ありがV003 84／88 B・X035 128 B。
これは.suに出力された関数単体の値で、callbackを含む積算stack量・実IRQを含むpeakではない。

入力JSONのgeneration欄に前実験から残っていた未使用8 bit候補を、今回実際に使う32 bitだけへ訂正した。
最終入力で28 ELFを再生成し、初回の28 image・28 ELFのhashと全サイズが一致した。
再生成は検証条件を追加したものではない。初回・最終の両記録を証拠へ含めた。

## 残る範囲

今回のPinsは宣言boolで、実clock／GPIO準備adapterへ未接続。
Domainは協調的・非取消の直列台帳、MMIOは既存MasterのIRQ-off・DMAなし・register独占を前提とする。
ISRからのenqueueや実際のIRQ禁止時間、実wireの停止・drain、clock喪失、sleep／wake、sourceの動的破棄は検証していない。
callback contextとManager・bufferは借用回収まで生存させる。handlerが長時間戻らない場合の時間保証もない。
通知の必須／optional、発生元識別、公開API、loop／delayの自動進行はP5で判断する。

証拠は `work/exp03-i2c-event-20261021-evidence.tar.gz`、3,733,710 B、
SHA-256 `5c5eb2337635043a30dcc0c9d106ac165211c7a6dcfe823b27892275b4bad79f`。
native実行物2本・162 trace・初回／最終各28 ELF・入力・tool記録・JUnit・再集計script・差分対応表を含む。
[変更課題](i2c-event-exercise.ja.md)は利用者レビュー前である。
