# P2〜P4 完了条件の監査と残作業

2026-10-22更新。採用方式を決めるレビューではなく、通常モデルが実行を続けるための完了条件監査。
基準は [実行仕様](../../experiment-tasks.ja.md)、[比較契約](../../experiment-spec.ja.md)、
[用途と変更課題](../../workloads.ja.md)。製品実装の現状を最終構成の制約にはしない。

## 現在地

P0／P1の設計・比較条件は作成済み。P2／P3はhost・cross buildの証拠が多いが、
P4の完成用途・全体費用・実機・人の変更課題評価は未完了。
この監査もP2〜P4の終了宣言、P5開始許可、最終仕様の採用にはしない。

実験数やテスト件数を分母にした進捗率は使わない。同じ部品の条件数を増やしても、
未接続の用途、物理時間、教材の理解しやすさが検証済みになるわけではない。

| 完了条件 | 現在の根拠 | 判定と残り |
|---|---|---|
| P2 再現手順・固定入力 | EXP-00の36 build、固定資料・tool・trace、上流K0／X0の再取得と4 ELFずつ | compile／hostの範囲では証拠あり。実起動・実周辺との接続は不足 |
| P2 K0／X0のR対照とUとの差 | K0／X0の独立oracle、上流source・wire呼出しの照合 | 仮想I/OのRは検証済み。上流と同等な物理backendでの用途比較ではない |
| P2 設備不足の明示 | 各resultの未検証欄と実行状況表 | 必要機材・相手・観測を一つの実施表へまとめる作業が残る |
| P3 必要部品・契約・費用・拡張 | EXP-01〜05のresultとmeasurements | 部分成立。文脈・借用・通知等の部品証拠はある。用途接続、実IRQ、深さ対照などは未完了 |
| P4 K0／X0完成用途比較 | 同一入力のR/A/B/C、容量変更・機能除去・故障trace | hostの用途ロジック比較。実backend・総費用・実測までを満たさない |
| P4 変更課題5件 | 設定2件、表示交換、タッチ追加の素材と複数のworksheet | queue削減・切断復旧を含む同一条件の変更差分集計が不足。人の評価は全件未実施 |
| P5入口 D01〜D12の支持・反証・未検証 | 下表 | 索引の初版。重要な未接続が残るので、判断材料が揃ったとは扱わない |

## D01〜D12の証拠索引

「支持」は記載した条件で候補が成立した証拠であり、候補を採用する推奨ではない。
各resultから固定入力・コマンド・測定manifest・移送archiveを辿る。
この索引では異なるABI、LTO、機能量、仮想時間の数値を合算・順位付けしない。

| 判断 | 成立を支持する証拠 | 反例・費用・比較限界 | 未検証と公開境界への影響 |
|---|---|---|---|
| D01 共通化の中心 | [K0](../EXP-06/k0-result.ja.md)、[X0](../EXP-06/x0-result.ja.md)、[typed wire](../EXP-06/x0-wire-result.ja.md)と[engine・台帳の用途接続](../EXP-06/x0-register-wire-result.ja.md) | X0のCはC-static-batchで汎用EventExecutorと同機能ではない。R/A同値も全条件で成立しない | event・準備・実I/O接続、完成用途費用。A/B/Cを選ぶ根拠が変わり得る |
| D02 stack | [work](../EXP-03/work-preparation.ja.md)、[wait](../EXP-03/wait-result.ja.md)、[深さ1／2／4](../EXP-03/reentry-depth-result.ja.md)で非再入・文脈・有限依存呼出しを検査 | 深さを増やしてもcycle・caller保持は解消しない。制限で戻るcalleeもframeを使う。解析call経路と単一frameの.suは観測peakではない | 適合portの別stack対照、IRQ込みpeak。再入・stack提供範囲に直結 |
| D03 自動進行 | K0／X0と[待機policy](../EXP-03/wait-result.ja.md)のM/S/U・文脈表 | Delay／AfterLoopは刺激として与えた進行機会。実runtimeに組み込んだ証拠ではない | 長時間処理・実PD／I2Cとの共存。delay内／loop外の許可範囲に直結 |
| D04 配信 | [配信結合](../EXP-03/completion-delivery-result.ja.md)、[fan-out](../EXP-03/completion-fanout-result.ja.md)、[optional](../EXP-03/completion-optional-result.ja.md)、[X0完了hint](../EXP-06/x0-event-wire-result.ja.md) | enqueue時ack＋解除で再通知を失う。required宛先停止は次の回収にも波及。X0の1結果保持で計測／touchも待つ。metadata配信とbuffer貸出は別 | 用途でのrequired／optional接続、発生元寿命と破棄。共通eventの公開範囲に直結 |
| D05 I/O | [回収分離](../EXP-01/i2c-handoff-result.ja.md)、[資源管理](../EXP-01/i2c-managed-handoff-result.ja.md)、[X035結合](../EXP-03/x035-i2c-event-result.ja.md) | [arbiter](../EXP-03/completion-arbiter-result.ja.md)の受理公平性の反例。別Managerの同値tokenは[誤配信](../EXP-03/i2c-event-result.ja.md)を識別できない | 実IRQ・停止保証・用途と同一backend。Busy／予約／通知／回収の契約に直結 |
| D06 待機 | [wait](../EXP-03/wait-result.ja.md)、[依存](../EXP-03/dependency-result.ja.md)でproducer条件と禁止文脈を明示 | 実際にproducerが進めるかはadapterの宣言。期限後に観測した成功を期限内保証にしない | queue・I/O・実時計を一緒にした待機。同期wrapperの適用範囲に直結 |
| D07 同期と容量 | [データ](../EXP-02/result.ja.md)、[共有と寿命](../EXP-02/shared-result.ja.md)、[取得buffer](../EXP-02/acquisition-result.ja.md)、[H417並列](../EXP-05/h417-feature-thread-result.ja.md) | host mutex／atomicの成功はPFIC・HSEM・cacheの成功ではない。poolとI/O回収は異なる | 実ISR共有・複数producer・実コア間同期。policy／classと所有・固定容量に直結 |
| D08 runtime | [再現](../EXP-00/result.ja.md)、[入口](../EXP-01/runtime-result.ja.md)、[時刻](../EXP-01/logical-time-result.ja.md)、[予約](../EXP-04/region-result.ja.md) | 起動命令検査は実起動ではない。未使用部品0 Bは登録済み容量の回収を意味しない | 実counter、観測間隔、初期化・予約込みB1。常設機能と除去境界に直結 |
| D09 保存 | [形式](../EXP-04/format-result.ja.md)、[store](../EXP-04/store-result.ja.md)、[走査・更新統合](../EXP-04/integrated-result.ja.md) | 1消去単位と2 bankは保証が異なる。予算付きreadでもerase／programは同期。中断模型は実電断全域を表さない | 用途共存、媒体停止、再program、upload保持。RAW／key・容量・保証に直結 |
| D10 休止 | [V006資料](../EXP-06/v006-sleep-result.ja.md)、[flow](../EXP-06/v006-sleep-flow-result.ja.md)、[生きたlease](../EXP-06/v006-sleep-lease-fault-result.ja.md) | 登録漏れは一覧だけでは見つからない。未回収leaseで休止できないこととfault公開は別 | 実driver停止・clock復帰・wake競合・電流。休止調停と時間の意味に直結 |
| D11 系列 | [X035 route](../EXP-01/x035-i2c-route-result.ja.md)、[H417資料](../EXP-05/h417-preparation.ja.md)、[配置](../EXP-05/h417-layout-result.ja.md)、[予約](../EXP-05/h417-reservation-result.ja.md) | F7非対応pin・debug保持条件。link済みopaque領域は共有C++ object起動ではない。HSEM記述差が残る | 別系列backend、両コア起動・可視性・reset。共通規則と規模別構成に直結 |
| D12 公開と教育 | [表示交換](../EXP-05/display-result.ja.md)、K0／X0と各exercise、[76候補対応](feature-evidence.csv) | worksheet作成・モデルによる解答は利用者評価ではない。汎用部品で未調査の周辺を対応済みにしない | 変更差分集計・人のレビュー・各候補の採否。提供場所・教材・対象外の判断が残る |

## 契約ごとの到達範囲

| 契約 | 主な検査先 | 到達範囲と不足 |
|---|---|---|
| IO-01 | I/O model、I2C master、handoff、managed | hostで受理・Busy・終端。実IRQでの原子性は未検証 |
| IO-02 | handoff、managed、取得buffer | hostで借用・停止宣言・回収。実hardware停止とDMA参照消失は未検証 |
| IO-03 | I/O model、I2C、arbiter | hostで取消と終端の順序・二重回収。実IRQ競合は未検証 |
| IO-04 | event、completion、I2C event | 宛先解除・世代・古い要求の拒否は検査。下記のend／callback境界は未確定 |
| DATA-01 | データ、共有、K0／X0 | hostで順序・満杯・部分処理。実IRQ下の同じ性質は未検証 |
| DATA-02 | 共有、event、wait | 決定的注入とhost thread。実割込み境界とwake原子性は未検証 |
| DATA-03 | データと共有 | hostでflag／semaphore／lock／pool。実backend・禁止時間は未検証 |
| RUN-01 | work、event、K0／X0 | 有限snapshotと非再入。handlerの所要時間保証・強制中断は含まない |
| RUN-02 | work、event、wait、用途 | M/S/Uと文脈を入力して検査。実delay／loop配置は未実装 |
| RUN-03 | wait、依存 | producerの直列刺激。IRQ／別コアが物理的に進むことは未検証 |
| TIME-01 | timer、clock、論理時刻 | 仮想周回・半周・端数・不連続。実sourceと観測上限の成立は未検証 |
| TIME-02 | timer、論理時刻 | 周期集約と通知保持。実時計停止・復帰との接続は未検証 |
| STORE-01 | region、format、store、scan、integrated | 宣言媒体模型とlink範囲。実電断・upload消去・IRQ停止は未検証 |
| SLEEP-01 | V006各単位 | 順序模型・準備失敗・回収。実待機命令前後の通知消失は未検証 |
| CORE-01 | H417所有・並列・配置・予約 | hostで所有と同期、cross linkで配置。実可視性・cache・peer resetは未検証 |

IO-04は特に範囲を狭く読む。
`test_i2c_event.py::test_client_end_or_ack_after_enqueue_requires_explicit_result_cleanup` と
`test_completion_event.py::test_lifetime_and_backpressure` のend条件は、
生きている配信先の内部handlerを1回呼び、回収を拒否してskipする。
これは利用者callbackが破棄後に呼ばれないことの証明ではない。
配信contextは検査中生存しており、client endとcontext破棄を同一視しない。
内部hint処理と公開callbackの区別、破棄前の解除・drain責任はP5へ渡す未確定事項。
通常モデルは反例と費用を追加できるが、IO-04を緩めて合格扱いにはしない。

## 測定の共通不足

- B0：複数の部品で未使用時の除去を測定済み。ヘッダ参照、archive抽出、登録無効、登録後解除を同じ条件にはしない。
- B1：用途ELFには仮想backend・計数器が含まれる一方、物理startup／IRQ／stack peak等が欠ける。Flash・静的RAMの値だけで完成用途のfitとはしない。
- B2：設定2件・タッチ・表示交換の比較がある。保存1単位を含む全体配置と残容量の同条件比較は不足。
- 時間：host tick、MMIO操作数、SCL clock算術は実測cycleではない。100 warm-up＋10,000操作×5 run、実用途60秒×5 run、較正した0／1／10／100 msの負荷は未実施。
- stack：.suの最大単一frameから呼出し経路・IRQ・入れ子を含むpeakを作らない。H417は両コアを分ける。
- 乱数：例えばK0は5 seed×5,000選択、X0は10 seed×3,000入力。P1初期条件の10 seed×10,000操作と同じではない。既存結果は元条件のまま保存し、不足分は追加runとして記録する。
- 人の評価：全exerciseはレビュー前。人の経験・実施順・予測・誤解・支援・変更結果をまだ記録していない。

## 通常モデルで続ける作業

各行は完了成果物を定めた作業単位。これらを順に閉じ、実機や高位判断待ちを理由に独立作業を止めない。
同じ部品の条件追加を、この表の用途接続や計測の代わりにしない。

| ID | 作業・終了条件 | 依存・停止条件 |
|---|---|---|
| N01 X0共通wire adapter | [host／ABI実施済み](../EXP-06/x0-wire-result.ja.md)。固定goldenとheader・2回RX、frame途中の失敗・回収・旧値を比較。84 trace・正本ABI68。後続N02でRX2末尾を検査 | 実験内部に隔離。未知のpanel初期化・CNVR clear・touch機器は推測せず残す。コピー方式の内部境界で、公開I/O方式の採用ではない |
| N02 X0周辺所有と進行 | host／ABI実施済み。[直接](../EXP-06/x0-register-wire-result.ja.md)、[通知](../EXP-06/x0-event-wire-result.ja.md)、[RX2末尾](../EXP-06/x0-rx-tail-result.ja.md)、[X035準備](../EXP-06/x0-prepared-wire-result.ja.md)を用途接続。準備はnative182件・ABI546、840 trace | 実機・read命令間IRQ・公開callback破棄・変換品質は未検証。N02の模型範囲の終了をP4や全契約の終了にしない。次はN03 |
| N03 K0共有境界 | [共有部分実施済み](../EXP-06/k0-shared-result.ja.md)：native118件・ABI132。全群の共有frontendで差分・FIFO・snapshotと注入を検査。C-bridgeは追加条件。[slave資料照合](../EXP-06/k0-slave-source-result.ja.md)でACK時点・STOPF条件・再START観測の不足を記録。[条件付きregister接続](../EXP-06/k0-slave-register-result.ja.md)：native137件・ABI132。51組の全群状態／MMIO一致。2拒否policyの意味差とforeign再STARTの反例を保存 | 上流のlive register契約へ変更しない。host／MMIO link範囲は実施済み。実ACK・拒否後復帰・他アドレス再START・実IRQは未検証でN09／N10へ残す。次の独立作業はN04 |
| N04 待機・再入対照 | [深さ比較実施済み](../EXP-03/reentry-depth-result.ja.md)：native78件・ABI36。継続型と深さ1／2／4、循環・active資源保持・外部完了を比較し、生成call経路を算定。別stackの固定port適合・最小配置・同じ仮想I/O列は未完了 | 別stack portがなければhost値をMCU値にしない。最終方式の採用はしない |
| N05 保存と用途共存 | 同期erase／programの間に通常処理が進まない媒体模型を用途へ接続。保存なし／1／2／4単位、再走査・故障・未回収の影響と全体配置を比較 | 宣言停止時間の感度分析と実停止を分離。PDの実必須期限を架空の値で作らない |
| N06 休止・コアの接続 | 既存所有・時刻・実行器を使うhost接続例を完成。V006の受理閉鎖〜回収〜復帰、H417のmessage公開〜通知〜返却と片側停止を通す | 実待機・HSEM・cache・resetを代替しない。未解決資料の解釈を採用せず境界を記録 |
| N07 変更課題の機械比較 | 5課題すべてについてbaseline→変更版の差分、編集file、追加状態、意味の変更、host trace、ABIを同じ様式で記録 | 故障検査があるだけで「切断復旧の追加課題を実施済み」としない。人の成績は別 |
| N08 測定条件の補完 | seed／操作数の差を一覧化し追加run。N01〜07の同条件B0／B1／B2、初期化・予約・stack予算の不足を一つの表にする | 未観測stackはnull。予算候補を実測peakとして扱わない |
| N09 実機実施依頼 | fixture identity、対象part、配線、相手、計測器、較正、lock、終了条件、run数、回復手順を用途別にまとめる | 設備確認前は書込みしない。既存fixtureがあるだけで今回の用途が成立したとしない |
| N10 横断監査更新 | 各Nのresultをこの表、契約表、D表へ反映。公開境界へ影響する不足と独立未検証を区別しP5依頼を作成 | 全項目を終了／条件付き未検証として根拠付きで処理する。通常モデル作業を高位判断へ押し付けない |

### N01の固定範囲

表示は既存128×64・3×5 glyph・4 goldenとpayload上限32 Bを維持する。
SSD1306 command／control byteはpayloadとは別計上する。前回のaddress順の模型を期待wire台帳へ用い、
frame／chunk途中の失敗後に部分frameを完成扱いしない。
panel固有の電源・COM／SEG・charge pumpを含むfull initは、この単位では未実装と明示する。

INA219は固定資料と上流設定のpointer write＋2 byte readを2量分扱い、
wire上のraw wordと解釈済みの値・有効／staleを分離する。
2回のreadが同一変換周期の同期snapshot、100 msごとにfresh、CNVR read-clearだとは仮定しない。
一方だけ失敗したとき、P1の計測結果を成功として公開しない。
raw取得と公開値のadapter内部契約を記録し、P1の失敗後stale規則を変えずに接続する。
実機の測定品質・新変換判定を選ぶ必要が出た場合は未決定として切り離す。

タッチはP1の8 B・50 Hzの仮想負荷を維持する。未指定controllerのcommandを作らない。
実touch controllerのwire比較は対象固定後の別条件として扱う。

## 実機・人・高位モデルに残すもの

実機は実起動、周波数・精度、IRQとI/O成立、PD相手、stack peak、禁止時間、電流、実電断、
upload保持、H417両コアの可視性・再起動。必要な観測が揃わない部分は未検証のまま残す。
人には5変更課題の予測・変更・誤解・支援の記録を依頼する。
高位モデルにはP5の採否・公開境界・系列構成・保証を判断してもらう。
通常のadapter実装、primary source照合、測定、実装不良修正は高位モデル待ちにしない。

機能候補76項目は [feature-evidence.csv](feature-evidence.csv) に1項目ずつ対応づけた。
関連する共通部品があっても、その周辺モードの実装証拠にはしない。
全76機能をP4までに実装する要求ではなく、P5で未検討のまま取り落とさないための索引。
元の現状欄には並行作業前の記述があるが、この対応表でUSB／Networkの現行実装を監査・上書きしない。

## 証拠の保全監査

[evidence-inventory.json](evidence-inventory.json) はmeasurement JSONの現在のhashと、
その中に明示されたlocal移送archiveの存在・byte数・SHA-256を照合した機械的索引。
archiveがない形式も欠落として隠さず列挙する。仕様適合の意味をhash一致から導かない。
過去の測定入力hashを現在の変更済みsourceへ一律照合せず、歴史的測定のsnapshotは各resultを参照する。
再生成は `python3 tests/platform/core_redesign/inventory_evidence.py --out work/<new-report>.json`。
N04深さ比較反映後の60 manifest、57 archive参照（重複を除く56個）は全てbyte数・SHA-256が一致。
archive参照がmanifest内にない4件は報告内に明記し、移送性まで確認したとは扱わない。
対象は測定記録であり、コア・USB／Networkの並行変更を変更・stageしない。

検証は相対リンク、76 IDの原表との順序・一意対応、全候補の採否未決定、参照resultの存在、
inventory再計算一致を確認。小さい独立fixtureではarchive正常、内容改変、欠落、
`archive_sha256`形式、archive参照なしの検出も確認した。
`tests/unit/test_workspace.py` は3件成功（既存warning 6件）。JUnitは
`work/p4-audit-20261021/workspace.xml`。conftestは追加していない。
これらは索引と配置規則の検査であり、実験全件の再実行やremote CI成功ではない。
