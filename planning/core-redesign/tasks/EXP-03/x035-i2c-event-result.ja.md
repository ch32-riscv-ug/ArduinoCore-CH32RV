# X035準備adapter・資源管理・完了callbackの接続

2026-10-21。実装commit `bc61d31`。
[入力](../../x035-i2c-event-inputs.json)、[全134 ELF](x035-i2c-event-size-matrix.csv)、
[host・tool・hash・集計](x035-i2c-event-measurements.json)を保存した。
[既存のroute／package準備adapter](../EXP-01/x035-i2c-route-result.ja.md)を、
[資源管理付きI2Cとcallback](i2c-event-result.ja.md)のbool Pinsの代わりに接続した。
既存headerは変更していない。製品コア・USB・Networkも編集していない。
これはX035 register模型と独立linkの実験で、実clock・実pin・wire・IRQの検証ではない。

## 接続で確認する境界

準備adapterと2個のManagedは同じDomainを参照する。
prepareは自分のleaseをclaimし、公称PCLK・gate・controller idleを確認してrouteとpin設定を書き、成功時にそのleaseを返す。
その後のManaged.open_backendは別のleaseをclaimしてadapter.readyを呼ぶ。
readyは以前のpreparedフラグだけを信用せず、公称PCLK・gate・idle・route・pinを再確認する。
MasterがopenするとPEが有効になるため、readyを通信中に再観測してfalseになるだけでは設定喪失の証明にならない。

| 状態 | prepare | 周辺close | 旧buffer／結果 |
|---|---|---|---|
| 準備前・資源free | callerのcold/quiescent宣言と検査後に設定 | まだopenしていない | なし |
| Managedがopen、要求なし | ResourceBusy | 許可 | なし |
| Queued／Running／reap前 | ResourceBusy | 拒否 | 借用継続 |
| reap後、Managedはopen | ResourceBusy | 他にQueued／Runningがなければ許可 | 結果と借用を保持 |
| Managed close後 | 再準備可能 | 重複closeは拒否 | take／callbackまで保持 |
| prepareが途中失敗 | FaultHeld、lease保持 | Managedはopenできない | 旧結果とは別の状態 |

再準備は未回収の利用者結果を消去しない。
Aの結果を残して周辺を返し、PCLK宣言とRCC模型を12 MHzへ変更して再準備し、Bを100 kHzでopenできる。
Bの通信中にAの既存callbackを実行しても、Bのlease・周辺・bufferへ影響しない。
Aのcallback登録と要求tokenの発生元対応は固定し、前実験の同値token誤配信の制約を引き継ぐ。

prepareはcold/quiescentをcallerから受け取るが、稼働中の周辺を強制停止する処理ではない。
途中失敗ではgate等に部分的な変更が残るため、leaseを保持する。
abandonは外部のquiescent根拠を受け取って台帳を返すだけで、pin設定のrollbackやresetを行わない。
停止が確認できないまま「trueを渡せば復旧できる」と解釈しない。

## Hostの証拠

ASan／UBSan、`-Wall -Wextra -Werror`、O0でnative192件＋workspace3件、計195件が通過した。
C8／F7・route 0〜7の16実行物を作り、それぞれで専用3 record／共有2 record方式を選ぶ。
192 trace・4,372操作を保存した。最後には各traceの全外側bufferを回収した。

- 対応14 package／route×2 record方式×4 wire形状の112条件。read 1／2／3 byteとtx 2＋read 3について、準備後の実験register engineからcallback経由で元bufferを回収した。
- gateの他bit、remapの他bit・debug設定、対象外portのpin設定を保持し、選択したpin fieldだけを書き換える。
- F7の非対応route 0／1はprepareをUnsupportedPackageで拒否する。Managed openもPinsNotReadyでrollbackし、pin／I2C registerへのアクセスはない。
- debug routeは既にdebugが解放された状態を要求する。解放されていなければpin／remapを書き換えずFaultHeldで資源を保持し、勝手にdebugを無効化しない。
- 全対応routeで、prepare後にrouteを変えるとManaged openが失敗する。失敗したopenは自分のleaseを返し、再prepare後はopenできる。
- route 0／2のgate・remap・pin書込みを個別に無視させる。部分失敗のlease保持、abandon(false)拒否、外部停止根拠後の返却と再試行を確認した。
- preparedフラグが残っても、HSI確認bit・分周・I2C gate・pin設定の変化によりopenを拒否し、leaseをrollbackする。
- route 0／2で、reap後かつclose前のprepare拒否、旧結果を保持中の再準備、別owner通信中の旧callback回収を確認した。

CheckedBusは16／32 bitのMMIOとcritical enter／leaveについて、論理資源leaseの保持をassertする。
明示ready／pclkだけは直列文脈の読出し観測として扱う。
BoardBusはgateのないregisterアクセスをassertし、callback等にMMIOが混ざらないことも各操作後のtraceで検査する。
これは他の書込み元の不在や実IRQ排他を証明するものではない。

32 bitアクセスにもlease assertを追加した最終版で195件を再実行し、追加前の192 traceと全出力が一致した。
元のMaster・Fake・Board Fake・route／setup・Domain・Managed・配信glue・EventExecutor等のhashも以前の記録に一致する。
route単体やcallback単体の全試験を重複したものではなく、今回の接続に必要な境界を検査した。
fixtureはmodule内に置き、conftest.pyは追加していない。

## 費用と.data

X035 rv32imac／ilp32だけを対象に、LTO off／onで134 ELFをlinkした。
各LTOにつき、空1・bool Pins基準2・2 package×8 route×2 record方式×使用／未使用の64構成。
bool Pins基準4 imageは前実験のimage hash・RAM・objectサイズ・最大単体frameと一致した。

LTOありの対応routeでの値は次のとおり。全体は2 Manager・Domain・buffer64 B×3・2宛先FIFO各1・観測harnessを含む。

| package | record方式 | bool基準image B | 準備接続image範囲 B | image差 B | 基準RAM B | 接続RAM B | RAM差 B |
|---|---|---:|---:|---:|---:|---:|---:|
| C8 | 各専用3 | 4,688 | 5,368〜5,416 | 680〜728 | 784 | 800 | 16 |
| C8 | 各共有2 | 4,688 | 5,368〜5,424 | 680〜736 | 712 | 728 | 16 |
| F7・対応route | 各専用3 | 4,688 | 5,368〜5,416 | 680〜728 | 784 | 800 | 16 |
| F7・対応route | 各共有2 | 4,688 | 5,368〜5,424 | 680〜736 | 712 | 728 | 16 |

C8 route 0単体では、専用image 5,376 B／共有5,384 B。
Flash差はprepare・abandon・ready・公称PCLK観測の追加経路と初期値を含み、実アプリの最小増分ではない。
F7非対応routeのimageは拒否処理を含む構成であり、成功する通信の小型化結果として比較しない。

準備adapterは16 Bで、bool Pins 1 Bからobject自体は15 B増える。
Domain参照とticket・flagsを持ち、static初期値が.data 16 Bとなる。
RAM全体の差はlink時の整列を含み、LTOあり16 B・なし12 Bである。全使用構成のRAMは専用800 B／共有728 B。
.dataをBSSへ隠したり初期値を費用から除外したりせず、imageへ格納する16 Bも含めて数えた。
startupや.data copyを実行したものではなく、その実行時間や起動後の実RAM状態は未検証。

未使用64構成はadapter・Manager・Domain・buffer・event・RAMの追加が0 Bで、空の8 B imageとhashも一致した。
allocator・constructor arrayは検出していない。
LTOありの最大単体frameは全使用構成128 B、なし96 B。呼出しの積算や実IRQを含むstack peakではない。

## 残る範囲

今回のRCC／gate／pinは模型で、実HSI精度・bus波形・pin衝突・clock切替中の可視性は未検証。
論理mask 7は実padの利用者台帳ではなく、USB／debug等との物理的な共存を保証しない。
既存MasterのIRQ-off・DMAなし・register独占という前提を引き継ぐ。
実IRQ共有、clock喪失中の停止、sleep／wake、resetやsource破棄時の回収、実機の時間・電力は残る。
公開API、発生元識別、配達機会の自動化とFaultHeldからの終了方針はP5で判断する。

証拠は `work/exp03-x035-i2c-event-20261021-evidence.tar.gz`、12,655,762 B、
SHA-256 `92b612251a9abaea57703b92425b455f466e097c8651666f6c434c46802ff134`。
最終native16実行物・192 trace・134 ELF・全入力・tool記録・JUnit・再集計scriptを含む。
[変更課題](x035-i2c-event-exercise.ja.md)は利用者レビュー前である。
