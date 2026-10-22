# N06 shared transportとcore-local実行器

2026-10-22。実験実装 `12edebd`。
[入力](../../h417-transport-inputs.json)、[測定manifest](h417-transport-measurements.json)、
[ABI表](h417-transport-size-matrix.csv)、[変更課題](h417-transport-exercise.ja.md)。
既存Fabric／WorkExecutor／EventExecutorとproduction core・USB・Network・H417並行実装は変更していない。

## 分離した状態

[直列接続](h417-executor-result.ja.md)で一体だった状態を分けた。
SharedはFabric、epoch、hint、workerの受理可否、実行中turn、quiet宣言を持つ。
ViewはSharedとGateへのbinding、Endpointは各コア固有のexecutor／callback、途中lease、cursor、結果・計数を持つ。
Endpointは移動／コピー不可、通常callerは各Endpointにつき1つ。別コアは自分のEndpointを使う。
Sharedにuser callback／context pointer／executor登録を置かない。
既存Fabricのgate referenceはShared内のUngated memberを指す内部参照で、内部操作は全てViewのouter Gate内に限定する。

Gateには排他と公開・可視性を要求し、全Viewを同じ排他domainへ結ぶ。hostは共通のmutex.try_lockまたはatomic_flag acquire／releaseを使う。
異なるbackendへ誤って結んだViewを自動検出するAPIや、実CPUからcore indexを取得するbindingは実装していない。
transportの各操作だけを保護し、callback全体をそのgate内で走らせない。
SerialGate.enteredの共有feedbackは廃止し、失敗したcallerへBusyをそのまま返す。
Empty／Full／Closed／Stale／Rejected等と区別する。reserveのRejectedは容量／generation拒否をまだまとめている。
これらは実験用の結果型で、公開APIの採用ではない。

message16 B×4件、block64 B×2個、3所有規則、4 feature投影は維持する。
R／Aは同じ直接進行、Bはlocal WorkExecutor1枠、Cはlocal EventExecutor1宛先の集約hint。
callbackはmessage1件、block take／release1回、または最大8 byte read。
producerがdataを公開してticketを返す操作と、hint配送は別々で、epochをgate内で確定する。
empty確認とhintの消去を同じgate内へまとめ、busy時はhintを変更しない。
M／S／U・domain／opt-inと文脈の制限はlocal Endpointへ適用する。

## 受付閉鎖とcallbackの終了

open_turnはepoch／serial／coreを持つpermitを返し、実行中bitを立てる。
permitのserialを照合し、古いcloseが別のturnを解放しないようにする。
recoverは新しい共有操作を閉じるが、既にpop済みのlocal messageの処理がその後完了することはある。
そのcallbackのcloseまで実行中bitを残し、attest／restartは待つ。
途中blockの次の共有操作はClosedになり、復旧後は古いleaseをepochで無効化する。

close_turnもtry操作なのでBusyを返せる。Endpointはborrowedを残して返り、次の通常呼出しではcloseだけをretryする。
新しいhandlerを同時に進めない。Cleanupはcallbackを呼ばず、通常文脈ではMのDelay／AfterLoopからも実行可能な比較条件。
Setup／ISR／Callback文脈ではcleanupも実行しない。このhousekeepingの公開範囲はP5の採否対象。
objectの寿命は未回収permitを含めて保持する。destructorで解放したことにしない。

workerのenable=falseは新規turnを閉じる宣言であり、実CPU停止ではない。
既に動いているturnはそのまま記録される。全workerの停止宣言とactive=0、DMA quietの宣言を確認してattestし、
全bitが揃ってからrestartする。実DMA停止・cache・IRQの証拠ではない。

## 再入で見つかった反例

最初の並列負荷は成功したが、通常文脈で同じEndpointへ再入すると、borrowedを「前の呼出しから戻った後のcleanup待ち」と誤認した。
Rのopen直後の再入で親permitを早くcloseし、親の最後のcloseがStaleになる反例を別の失敗binaryで保存した。
共有側の排他だけでは、callbackの実行中と返却後のcleanup待ちを区別できない。

local in_turnを追加し、文脈引数がNormalでも実際の再入を拒否した。
permit取得後／message pop後／close直前×4候補の12境界で、親のactive bitとborrowedを維持し、
外側が戻るときだけcloseすることを検査した。修正後は並列検査とABIを全て再実行した。

## hostの証拠

最終ASan／UBSanはnative258件＋workspace3件で261件、TSanはnative256件。全て失敗・skipなし。
各sanitizerで4 binary、192負荷条件＋callback中復旧32条件＋close Busy回収32条件を実行した。
両sanitizer合計512 run、負荷部分のmessage49,152件、block12,288個を照合した。

- 4候補×3所有規則×4 feature×2 gate×2 seedの負荷。生成側は1 thread。
- Single／Fixedは生成1＋消費1の2 thread、Sharedは生成1＋消費2の3 thread。
- 各consumerの実行器・cursor・結果はprivate。共有データはgate下だけで触る。
- message pop／block takeのgate取得順ticketを記録し、callback完了順と分けてFIFOを検査。
- Pythonはpayload hash、generation、件数を独立に照合し、leaseの返却後に全poolがFreeであることを確認。
- callbackをpermit取得後／pop後／byte read後に止め、別threadのrecover・attest・restartが終了を待つことを確認。
- closeだけBusyになった状態、禁止文脈での保持、次の通常呼出しでのcleanupを確認。
- 別のtyped contract検査で出力値保持、空／満杯／閉鎖／古いpermit、disabled featureのgate呼出し0を確認。
- 4候補のM/S/U等72条件、文脈12条件、実際のnormal再入12境界を検査。

Singleの2 threadは同じ論理core indexの生成と通常処理を模したhost負荷で、実IRQ動作ではない。
同時に走るproducerは各run1つであり、新wrapperの2 producer負荷や継続的reset負荷を検証したとはしない。
それらとP1の乱数予算・全体予算の不足はN08で横断補完する。
TSan成功はこのhost gate条件の証拠で、H417のHSEM／cache backendへ読み替えない。

## 両coreの構造的link

[固定H417配置入力](../../h417-layout-inputs.json)のmarch／ilp32fと、FLASH／通常RAM／共有RAMのregionアドレスを使った。
4候補×3所有規則×4 feature×使用／未使用×2 core×LTO有無とempty、388 build。
使用192行、未使用192行、empty4行。全ELF flags3、allocator／constructor arrayなし。

shared_probeは共有regionのPROGBITSとし、非zero初期値をFlash側のload imageにも保持する。
96組でV3F／V5Fの共有VMA・size・初期byteが一致した。
View bindingとGateは各coreのprivate領域、Endpointは消費役のcoreだけへ配置した。
LTOで不要になったView bindingは除去される。残るbindingはprivate RAM／Flashにあり、共有regionへ置かれない。
未使用192行はemptyと同じimageで、共有・local静的RAMとも0 B。

以下はFixed／両機能有効／LTOあり。imageにはshared初期値、harness、opaque gate境界も含む。

| 候補 | V3F image B | V5F image B | V3F local RAM B | V5F local RAM B | shared RAM B（1回だけ） | 合計静的RAM B |
|---|---:|---:|---:|---:|---:|---:|
| R／A | 1,997 | 3,357 | 176 | 280 | 260 | 716 |
| B-work | 1,997 | 3,637 | 176 | 312 | 260 | 748 |
| C-notification-bridge | 1,997 | 3,613 | 176 | 328 | 260 | 764 |

生成側V3FにはEndpointがなく、候補差もない。消費側のEndpoint objectはR/A92 B、B128 B、C140 B。
local計数・harness・gate bindingを含む測定なので、これを製品の最小費用としない。
matrixのlocalとsharedを分け、sharedを2回足さない。imageの両core分には共通初期値が重複するため、そのload順を自動決定したものではない。
.su最大値は単一frameであり、実行中stack peakではない。

linkerは既存regionから作った実験scriptで、公式起動全体を実行するfirmwareではない。
このlinkは配置と初期byteの整合の証拠。ownerによる1回初期化、peer handshake、upload順、
実gate／cache、実起動・停止／reset・IRQ・時刻・全runtime fitは未検証でN08／N09へ残す。
opaque gateはno LTOの費用境界で、実機の排他を実装していない。

## 再現・履歴と次の作業

最終host：`work/exp06-h417-transport-host-20261022-r2`、TSan：`work/exp06-h417-transport-tsan-20261022-r2`。
contract／再入検査：`work/exp06-h417-transport-contract-20261022-r2`、`work/exp06-h417-endpoint-reentry-20261022`。
最終ABI：`work/exp06-h417-transport-abi-20261022-r2`、集計：`work/exp06-h417-transport-analysis-20261022`。

修正前はASan259件、TSan256件、contract1件、ABI388件が成功したが、別の再入reproducerがassertで失敗した。
修正後の最終値は上記に分離し、修正前成功を欠陥なしの証明とはしない。
ABI分析の初回はView symbolが常にあるとする期待で失敗し、LTOによるbinding除去を確認して集計を修正した。
prototypeやbuildをその期待へ合わせて変えていない。失敗履歴と修正前入力もarchiveへ含めた。

N06の休止接続と、H417の直列・host並列・構造的link比較を実施した範囲を記録した。
次はN07の変更課題を同じ様式で比較する。N08では2 producer／reset負荷、乱数・総費用・初期化等の不足も補う。
実機／利用者評価／P5の採否は未実施であり、P4全体の終了ではない。
