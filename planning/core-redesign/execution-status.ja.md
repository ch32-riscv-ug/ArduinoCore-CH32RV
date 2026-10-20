# P4までの実行状況

2026-10-21。利用者の指示は「P4までで進められるところを全て進め、レビュー・高位判断が必要なところを残す」。
実行方式の採用はP5で扱う。通常の実装修正・資料照合・試作・測定はこのセッションで続け、作業単位ごとにコミットする。
この一覧は完了監査にも使い、hostの成功で実機・利用者評価を埋めない。

| 領域 | 現在の証拠 | 進める作業 | 実機・レビュー等が残る部分 |
|---|---|---|---|
| P2 再現 | EXP-00、36 build一致 | 測定の拡張時も入力と生成物を記録 | 実runtimeとは別 |
| P2 用途対照 | PREP-01と02、用途のhost／ABI。[独立runtime ELF](tasks/EXP-01/runtime-result.ja.md) | 実起動・周波数と精度、I/O backend、上流のbuild専用再現 | 実IRQ共有、wire、PD、実機起動 |
| EXP-01 最小構成・I/O | [I/O所有model](tasks/EXP-01/io-preparation.ja.md)：host22件、ABI48。[reset／GPIO入口](tasks/EXP-01/runtime-result.ja.md)：host16・ELF40・拒否link12・初期化故障12。[clock/time](tasks/EXP-01/clock-time-result.ja.md)：host34・ELF40。[論理時刻](tasks/EXP-01/logical-time-result.ja.md)：host123・最終ABI52＋変更前52、端数・不連続とtimer境界。[I2C master](tasks/EXP-01/i2c-master-result.ja.md)：host59・ELF36 | 実counterとの接続・観測上限の根拠、I2Cのclock／pin／所有結合、ISRと資源引渡し・回収の接続 | 実起動・周波数・精度・周辺・IRQ時間 |
| EXP-02 データと同期 | [直列単位](tasks/EXP-02/result.ja.md)と[寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：host92＋31件、link200＋68構成、未使用36＋16行が0 B、TSan4条件 | 実行器への待機接続、ring／message／poolの共有条件 | 実ISR／コア間の同期と禁止時間 |
| EXP-03 実行・通知 | [work](tasks/EXP-03/work-preparation.ja.md)・[期限](tasks/EXP-03/timer-preparation.ja.md)・[宛先とI/O結合](tasks/EXP-03/event-preparation.ja.md)・[待機policy](tasks/EXP-03/wait-result.ja.md)・[資源依存](tasks/EXP-03/dependency-result.ja.md)：host151件、ABI212構成 | X0の変更課題、依存台帳の実backend接続、IRQ共有、完了枠／arbiter、別stack対照 | MCU時間・stack観測、採否はP5 |
| EXP-04 保存 | [媒体・領域準備](tasks/EXP-04/region-result.ja.md)：host138件、ELF40・想定link拒否80・artifact故障検出150。[形式比較](tasks/EXP-04/format-result.ja.md)：host200件、105容量条件。[C++試作](tasks/EXP-04/store-result.ja.md)：native272件・中断7,442条件・ABI198。[予算付き読出しと所有](tasks/EXP-04/scan-result.ja.md)：native353件・ABI76、最大32 byte／pollと古い結果の拒否 | 受理／key変更への走査・所有統合、非同期媒体と回収、予約とdomainの整合、所有期間・公平性、追記・整理、物理再program規則、driverと実upload経路 | 実電断・CPU／IRQ停止・upload保持 |
| EXP-05 拡張 | タッチ追加のR対照。[表示交換と独立renderer](tasks/EXP-05/display-result.ja.md)：host72件・ABI164、4 mock形式×R/A/B/C、独立build、未使用80行の追加0 B。[H417予備調査](tasks/EXP-05/h417-preparation.ja.md)：5 PDF・SDK21 file、公開順序と資料監査18件。[所有・回復](tasks/EXP-05/h417-ownership-result.ja.md)：native79件・ABI32、3所有規則・未使用12条件の追加0 B。[host並列](tasks/EXP-05/h417-thread-result.ja.md)：ASan／UBSan25件・TSan22件。[機能除去](tasks/EXP-05/h417-features-result.ja.md)：native184件・ABI112、[feature並列](tasks/EXP-05/h417-feature-thread-result.ja.md)：ASan／UBSan83件・TSan80件。[共有配置](tasks/EXP-05/h417-layout-result.ja.md)：6 pair・link拒否3条件・配置検査36件 | 実controllerと所有への接続、実行器接続、共有予約の容量／未使用設定・共有表現・起動handshake、HSEM記述差の確認 | 実wire／render時間・cache可視性・両コア・reset |
| EXP-06 完成用途 | [K0 R/A/B/Cと容量2変更](tasks/EXP-06/k0-result.ja.md)：host82件、ABI116構成。[X0追加比較](tasks/EXP-06/x0-result.ja.md)：host55件、ABI116構成、同じ進行機会・故障・50Hzタッチ。[X0機能除去](tasks/EXP-06/x0-features-result.ja.md)：host88件・ABI116構成 | 表示交換・独立library等の変更課題、K0の物理接続 | 実通信・電流・PPS source・教材の利用者レビュー |
| V006休止 | [資料と待機境界](tasks/EXP-06/v006-sleep-result.ja.md)：4 PDF・固定SDK、host54件、304順序とAWU30条件。[結合](tasks/EXP-06/v006-sleep-flow-result.ja.md)：native64件・ABI36。[依存除去と故障](tasks/EXP-06/v006-sleep-features-result.ja.md)：native82＋156件・ABI64。[生きたleaseとの複合](tasks/EXP-06/v006-sleep-lease-fault-result.ja.md)：追加432件、未回収・逆順回収・古い終端、runtime入力は不変 | 実driver停止・再設定と待機adapter、adapterの受理閉鎖・一覧の完全性、IRQ接続。回収不能の終了方針・未回収の表示はP5 | option byteと実復帰、wake競合・可視性、静止電流 |
| 取得buffer | [2 block所有と回収](tasks/EXP-02/acquisition-result.ja.md)：native50件・ABI32構成、生成元先行／利用側停止と逆順返却、未使用12条件の追加0 B | 完了枠との結合、所有者識別・実ISR共有、停止／再設定のadapter契約 | 実ADC/DMA、循環書込み停止・可視性、物理欠落と時間 |
| P4教育 | 教材案、[X0機能除去](tasks/EXP-06/x0-feature-exercise.ja.md)、[時刻読出し](tasks/EXP-01/time-read-exercise.ja.md)、[I2Cの貸出](tasks/EXP-01/i2c-loan-exercise.ja.md)、[保存容量と保持](tasks/EXP-04/region-exercise.ja.md)、[形式と更新途中](tasks/EXP-04/format-exercise.ja.md) | 残る変更課題の教材化、変更差分・必要状態の比較 | 人が予測・変更した結果のレビュー |

不足する設備や上位判断は該当部分の未検証として残し、独立した作業を続ける。
型番・資料・相手・観測がない項目を、既存fixtureの存在やcompile成功だけで実施可能と決めない。
実機操作を始める場合は既存の明示fixture・identity・lock・終了条件を使い、接続先を推測しない。

## 最新の完了単位

- [P2-PREP-01](tasks/P2-PREP-01/result.ja.md)：対象・個別ロジック。
- [P2-PREP-02](tasks/P2-PREP-02/result.ja.md)：X0の共有bus・周期・結合ロジック。
- [EXP-02直列単位](tasks/EXP-02/result.ja.md)：データ部品・容量・未使用除去。
- [EXP-02寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：move-only、gate、登録競合、MPSC／SPSCのhost検査とABI比較。
- [EXP-02取得buffer](tasks/EXP-02/acquisition-result.ja.md)：2 blockの生成元／利用側所有、通知と回収、取消と停止、部分生成、入力契約別の負荷集計。[課題](tasks/EXP-02/acquisition-exercise.ja.md)は人による評価前。
- [EXP-03有限work準備](tasks/EXP-03/work-preparation.ja.md)：有限snapshot、再投稿、公平性、M/S/U、非再入と解除。
- [EXP-01 I/O所有model](tasks/EXP-01/io-preparation.ja.md)：受理、取消、sticky終端、世代、end、buffer回収。
- [EXP-03期限model](tasks/EXP-03/timer-preparation.ja.md)：半周・周回・周期集約・未回収通知・停止／再設定。
- [EXP-03宛先結合](tasks/EXP-03/event-preparation.ja.md)：FIFO／集約bit、満杯でもI/O・timer保持、fan-out、配達前解除。
- [EXP-06 K0同等比較](tasks/EXP-06/k0-result.ja.md)：同一入力のR/A/B/C、M/S/U、設定容量2の変更課題。
- [EXP-03待機policy](tasks/EXP-03/wait-result.ja.md)：try／非同期／同期、A/B/Cとproducer進行範囲、禁止文脈・期限観測。
- [EXP-03 資源依存](tasks/EXP-03/dependency-result.ja.md)：保有したbus／buffer／lock、既知・未知の依存、同期と明示poll。
- [EXP-06 X0追加比較](tasks/EXP-06/x0-result.ja.md)：R/A/B/C-static-batch。固定1宛先の追加条件を汎用Cと区別。

- [EXP-01 reset／GPIO入口](tasks/EXP-01/runtime-result.ja.md)：vector・data／bss・ctor呼出しの命令検査、raw／class比較。実起動は未確認。
- [EXP-01 clock/time](tasks/EXP-01/clock-time-result.ja.md)：有限clock切替、32／64 bit sample、epoch、変換費用と情報欠落の例。
- [EXP-01 論理時刻](tasks/EXP-01/logical-time-result.ja.md)：上限宣言／推定／native64、端数、既知／不明停止、timerのdomain・半周guard。実sourceの保持・間隔・精度は未検証。
- [EXP-01 I2C master](tasks/EXP-01/i2c-master-result.ja.md)：有限register進行、受信末尾・取消と貸出、容量8／32／64、未使用8条件の追加0 B。実通信・IRQは未検証。
- [EXP-04 媒体・領域準備](tasks/EXP-04/region-result.ja.md)：読出し・program・eraseの粒度、保守的な中断模型、予約とlink／image検査。保存の実機成立・実電断・upload保持は未検証。
- [EXP-04 形式比較](tasks/EXP-04/format-result.ja.md)：RAW／blob／数値key、上書き／2 bank／別単位の完了印。条件付きhost模型であり、実機の保護は未検証。
- [EXP-04 C++試作](tasks/EXP-04/store-result.ja.md)：初回／置換の模型照合、作業buffer・ABI費用と未使用除去。同期走査の読出し回数も記録し、実媒体・所有・走査分割は残す。
- [EXP-04 予算付き読出しと所有](tasks/EXP-04/scan-result.ja.md)：明示pollのbyte上限、共有台帳の重複拒否とticket、結果保持と解放後の無効化。書込み側への統合・ISR／core間共有は残す。
- [EXP-06 X0機能除去](tasks/EXP-06/x0-features-result.ja.md)：固定off／on・専用対照・旧runtime比較と変更課題。
- [EXP-06 V006資料と待機境界](tasks/EXP-06/v006-sleep-result.ja.md)：保持・clock・SDK入口、条件付きの待機反例、AWUの設定時間幅。[教材](tasks/EXP-06/v006-sleep-exercise.ja.md)は人による評価前。V006用途全体と実機SLEEP-01は未完了。
- [EXP-06 休止準備・回収・時刻の結合](tasks/EXP-06/v006-sleep-flow-result.ja.md)：明示一覧と任意registryの同条件比較、部分復帰・失敗retry・登録忘れの限界。[分割処理の課題](tasks/EXP-06/v006-sleep-flow-exercise.ja.md)は人による評価前。実V006 adapter・待機命令・電流は含まない。
- [EXP-06 timer依存除去と段階失敗](tasks/EXP-06/v006-sleep-features-result.ja.md)：型で期限依存を除去、同一ソースの対照、6段階のWaiting／Failedとclock喪失。起動・転送中故障の全組合せではない。[課題](tasks/EXP-06/v006-sleep-features-exercise.ja.md)は人による評価前。
- [EXP-06 生きたleaseとの複合](tasks/EXP-06/v006-sleep-lease-fault-result.ja.md)：Prepare／Abort失敗、転送中・終端未回収・片方回収のretryとticket再利用。432 traceを72群で比較。[課題](tasks/EXP-06/v006-sleep-lease-fault-exercise.ja.md)は人による評価前。
- [EXP-05 表示交換と独立renderer](tasks/EXP-05/display-result.ja.md)：build時にmock形式・chunk上限・配置を交換。型と入出力を接続し、部品単独／結合の費用と仮想wireを記録。[交換課題](tasks/EXP-05/display-exercise.ja.md)は人による評価前。
- [EXP-05 H417の一次資料と公開順序](tasks/EXP-05/h417-preparation.ja.md)：V3F／V5F・共有memory・IPC／HSEMの条件、固定sourceの監査と7 eventの順序模型。所有・再利用・DMA・resetと実機は未検証。
- [EXP-05 H417向け所有・回復](tasks/EXP-05/h417-ownership-result.ja.md)：message16 B×4とblock64 B×2、single／fixed／shared-accessの直列契約、静止宣言とepoch更新、79 traceを全操作で照合。この単位は直列。host並列同期は下記の追加検査を参照。実行器・実resetは未接続。[教材](tasks/EXP-05/h417-ownership-exercise.ja.md)は人による評価前。
- [EXP-05 H417向けhost並列同期](tasks/EXP-05/h417-thread-result.ja.md)：2 thread×mutex／atomic gate、fixed／shared、ASan／UBSan25件・TSan22件。既存所有header・ABI入力は不変。実行器・H417 backend・DMA・実resetは未接続。
- [EXP-05 message／block機能除去](tasks/EXP-05/h417-features-result.ja.md)：native184件・ABI112、3所有規則×4機能構成。本体16／84／168／236 B、未使用48構成の追加0 B。この単位は直列。feature型の並列検査は下記の追加結果を参照。実行器接続は残る。
- [EXP-05 feature型のhost並列](tasks/EXP-05/h417-feature-thread-result.ja.md)：ASan／UBSan83件・TSan80件、4機能構成×2 gate×fixed／shared-access。disabledのgate取得除去と有効payloadのFIFOを確認。header・ABI入力は不変。実行器とH417実backendは残る。
- [EXP-05 H417共有配置](tasks/EXP-05/h417-layout-result.ja.md)：固定SDK linkerで6 pair・12 ELF、共有3容量×LTO、想定link拒否3条件、配置検査36件。opaque配列の配置であり、C++共有object・実起動・upload保持は未検証。

次は論理時刻の実source・sleep接続とI2Cの周辺準備／所有／IRQ結合、X0の残る変更課題、保存・系列・休止、実共有条件を進める。
P2の実backend待ちで、pureな部品とhost契約の検査を止めない。
