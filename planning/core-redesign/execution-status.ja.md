# P4までの実行状況

2026-10-22。利用者の指示は「P4までで進められるところを全て進め、レビュー・高位判断が必要なところを残す」。
実行方式の採用はP5で扱う。通常の実装修正・資料照合・試作・測定はこのセッションで続け、作業単位ごとにコミットする。
この一覧は完了監査にも使い、hostの成功で実機・利用者評価を埋めない。

[P2〜P4完了条件監査](tasks/P4-AUDIT-01/result.ja.md)に、D01〜D12の支持・反例・未検証、
全契約の到達範囲、機能候補76項目との対応、通常モデルの残作業N01〜N10をまとめた。
部品数を進捗率に置き換えず、用途接続・実機・人の評価を別に扱う。

| 領域 | 現在の証拠 | 進める作業 | 実機・レビュー等が残る部分 |
|---|---|---|---|
| P2 再現 | EXP-00、36 build一致 | 測定の拡張時も入力と生成物を記録 | 実runtimeとは別 |
| P2 用途対照 | PREP-01と02、用途のhost／ABI。[独立runtime ELF](tasks/EXP-01/runtime-result.ja.md)。[X0上流再現](tasks/EXP-06/x0-upstream-result.ja.md)：4 ELF同一image・native16条件×2 copy。[K0上流再現](tasks/EXP-06/k0-upstream-result.ja.md)：4 ELF同一image・15 fileのGit blobも照合 | 実起動・周波数と精度、I/O backend、同等機能でのU／R実機確認 | 実IRQ共有、wire、PD、実機起動 |
| EXP-01 最小構成・I/O | [I/O所有model](tasks/EXP-01/io-preparation.ja.md)：host22件、ABI48。[reset／GPIO入口](tasks/EXP-01/runtime-result.ja.md)：host16・ELF40・拒否link12・初期化故障12。[clock/time](tasks/EXP-01/clock-time-result.ja.md)：host34・ELF40。[論理時刻](tasks/EXP-01/logical-time-result.ja.md)：host123・最終ABI52＋変更前52、端数・不連続とtimer境界。[I2C master](tasks/EXP-01/i2c-master-result.ja.md)：host59・ELF36。[I2C資源利用権](tasks/EXP-01/i2c-resource-result.ja.md)：native47件・ABI28。[X035準備](tasks/EXP-01/x035-i2c-setup-result.ja.md)：native100件・ABI18。[route／package](tasks/EXP-01/x035-i2c-route-result.ja.md)：native136件・ABI68。[I2C回収分離](tasks/EXP-01/i2c-handoff-result.ja.md)：native70件・ABI20、backend reapと利用者take。[資源台帳との接続](tasks/EXP-01/i2c-managed-handoff-result.ja.md)：native74件・ABI28、旧結果保持中の別owner・clock宣言変更 | 実counterとの接続・観測上限の根拠、物理pin競合とclock喪失／停止復帰、ISRと資源引渡し・回収の接続 | 実起動・周波数・精度・周辺・IRQ時間 |
| EXP-02 データと同期 | [直列単位](tasks/EXP-02/result.ja.md)と[寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：host92＋31件、link200＋68構成、未使用36＋16行が0 B、TSan4条件 | 実行器への待機接続、ring／message／poolの共有条件 | 実ISR／コア間の同期と禁止時間 |
| EXP-03 実行・通知 | [work](tasks/EXP-03/work-preparation.ja.md)・[期限](tasks/EXP-03/timer-preparation.ja.md)・[宛先とI/O結合](tasks/EXP-03/event-preparation.ja.md)・[待機policy](tasks/EXP-03/wait-result.ja.md)・[資源依存](tasks/EXP-03/dependency-result.ja.md)：host151件、ABI212構成。[完了枠予約](tasks/EXP-03/completion-arbiter-result.ja.md)：native22件・最終ABI20構成。[配信結合](tasks/EXP-03/completion-event-result.ja.md)と[直接回収との費用比較](tasks/EXP-03/completion-delivery-result.ja.md)：native76件・ABI28、満杯retry・宛先解除・end・古い通知。[metadata fan-out](tasks/EXP-03/completion-fanout-result.ja.md)：native356件・ABI108、1／2／4宛先と保持の波及。[用途側optional](tasks/EXP-03/completion-optional-result.ja.md)：native45件、全宛先待ち／1回try／最新状態。[資源管理付きI2Cとcallback](tasks/EXP-03/i2c-event-result.ja.md)：native128件・ABI28、2発生元の固定宛先と誤配信の反例。[X035準備との接続](tasks/EXP-03/x035-i2c-event-result.ja.md)：native192件・ABI134、全route／packageと再準備・旧結果配達 | X0の変更課題、依存台帳の実backend接続、IRQ共有、受理公平性、実周辺・IRQ／休止との結合、別stack対照 | MCU時間・stack観測、採否はP5 |
| EXP-04 保存 | [媒体・領域準備](tasks/EXP-04/region-result.ja.md)：host138件、ELF40・想定link拒否80・artifact故障検出150。[形式比較](tasks/EXP-04/format-result.ja.md)：host200件、105容量条件。[C++試作](tasks/EXP-04/store-result.ja.md)：native272件・中断7,442条件・ABI198。[予算付き読出しと所有](tasks/EXP-04/scan-result.ja.md)：native353件・ABI76、最大32 byte／pollと古い結果の拒否。[更新への統合](tasks/EXP-04/integrated-result.ja.md)：native234件・新規ABI76、既存読出し76 image不変 | 非同期媒体と回収、予約とdomainの整合、所有期間・公平性、追記・整理、物理再program規則、driverと実upload経路 | 実電断・CPU／IRQ停止・upload保持 |
| EXP-05 拡張 | タッチ追加のR対照。[表示交換と独立renderer](tasks/EXP-05/display-result.ja.md)：host72件・ABI164、4 mock形式×R/A/B/C、独立build、未使用80行の追加0 B。[H417予備調査](tasks/EXP-05/h417-preparation.ja.md)：5 PDF・SDK21 file、公開順序と資料監査18件。[所有・回復](tasks/EXP-05/h417-ownership-result.ja.md)：native79件・ABI32、3所有規則・未使用12条件の追加0 B。[host並列](tasks/EXP-05/h417-thread-result.ja.md)：ASan／UBSan25件・TSan22件。[機能除去](tasks/EXP-05/h417-features-result.ja.md)：native184件・ABI112、[feature並列](tasks/EXP-05/h417-feature-thread-result.ja.md)：ASan／UBSan83件・TSan80件。[共有配置](tasks/EXP-05/h417-layout-result.ja.md)：6 pair・link拒否3条件・配置検査36件。[共有予約](tasks/EXP-05/h417-reservation-result.ja.md)：8 pair・容量0含む、link拒否8・混在拒否3条件 | 実controllerと所有への接続、実行器接続、共有表現・起動handshake、HSEM記述差の確認 | 実wire／render時間・cache可視性・両コア・reset |
| EXP-06 完成用途 | [K0 R/A/B/Cと容量2変更](tasks/EXP-06/k0-result.ja.md)：host82件、ABI116構成。[X0追加比較](tasks/EXP-06/x0-result.ja.md)：host55件、ABI116構成。[X0機能除去](tasks/EXP-06/x0-features-result.ja.md)：host88件・ABI116構成。[device入力照合](tasks/EXP-06/x0-device-result.ja.md)：2 PDF。[typed wire](tasks/EXP-06/x0-wire-result.ja.md)：native46件・ABI68。[engine・台帳](tasks/EXP-06/x0-register-wire-result.ja.md)：native84件・ABI36。[通知](tasks/EXP-06/x0-event-wire-result.ja.md)：native53件・ABI68。[RX末尾](tasks/EXP-06/x0-rx-tail-result.ja.md)：native70件・新規ABI0。[X035準備](tasks/EXP-06/x0-prepared-wire-result.ja.md)：native182件・ABI546。[K0共有](tasks/EXP-06/k0-shared-result.ja.md)：native118件・ABI132。[slave register](tasks/EXP-06/k0-slave-register-result.ja.md)：native137件・ABI132 | N04待機・再入、残る変更課題、K0の物理接続 | 実通信・電流・PPS source・教材の利用者レビュー |
| V006休止 | [資料と待機境界](tasks/EXP-06/v006-sleep-result.ja.md)：4 PDF・固定SDK、host54件、304順序とAWU30条件。[結合](tasks/EXP-06/v006-sleep-flow-result.ja.md)：native64件・ABI36。[依存除去と故障](tasks/EXP-06/v006-sleep-features-result.ja.md)：native82＋156件・ABI64。[生きたleaseとの複合](tasks/EXP-06/v006-sleep-lease-fault-result.ja.md)：追加432件、未回収・逆順回収・古い終端、runtime入力は不変 | 実driver停止・再設定と待機adapter、adapterの受理閉鎖・一覧の完全性、IRQ接続。回収不能の終了方針・未回収の表示はP5 | option byteと実復帰、wake競合・可視性、静止電流 |
| 取得buffer | [2 block所有と回収](tasks/EXP-02/acquisition-result.ja.md)：native50件・ABI32構成、生成元先行／利用側停止と逆順返却、未使用12条件の追加0 B | 完了枠との結合、所有者識別・実ISR共有、停止／再設定のadapter契約 | 実ADC/DMA、循環書込み停止・可視性、物理欠落と時間 |
| P4教育 | 教材案、[X0機能除去](tasks/EXP-06/x0-feature-exercise.ja.md)、[時刻読出し](tasks/EXP-01/time-read-exercise.ja.md)、[I2Cの貸出](tasks/EXP-01/i2c-loan-exercise.ja.md)、[保存容量と保持](tasks/EXP-04/region-exercise.ja.md)、[形式と更新途中](tasks/EXP-04/format-exercise.ja.md) | 残る変更課題の教材化、変更差分・必要状態の比較 | 人が予測・変更した結果のレビュー |

不足する設備や上位判断は該当部分の未検証として残し、独立した作業を続ける。
型番・資料・相手・観測がない項目を、既存fixtureの存在やcompile成功だけで実施可能と決めない。
実機操作を始める場合は既存の明示fixture・identity・lock・終了条件を使い、接続先を推測しない。

## 最新の完了単位

- [N04 X035固定portと配置](tasks/EXP-03/stack-port-result.ja.md)：WCH source76 fileを固定し、最終ABI50・workspace3件。協調／preemption・アプリ2／3 task・stack64／128／256 wordを比較。未使用24行はemptyと同じimage／静的RAM。portのmscratch上端−512 Bを追跡し、起動／IRQ予約を1,024 Bへ修正。各512 Bのstack・2 task＋idleはLTOありFlash4,088 B、RAM総予約3,292 B。実切替・待機・peakは未測定。2 stackだけの協調portと同一仮想I/O列は引き続きN04。

- [N04 再入深さ](tasks/EXP-03/reentry-depth-result.ja.md)：native78件＋workspace3件・ABI36、137 trace。継続型と入れ子深さ1／2／4、循環・active資源保持・外部完了・禁止文脈を比較。未使用16行は追加0 B。制限で戻るcalleeにもframeがあり、callback深さと関数frame数の差を逆アセンブルで確認。解析経路値は実stack peakではない。別stackの固定port・配置・同じ仮想I/O列は未完了で、N04を続ける。

- [N03 条件付きslave register接続](tasks/EXP-06/k0-slave-register-result.ja.md)：最終native137件＋workspace3件・ABI132。207 trace・298,863コマンド、全4群の51組が一致。受信末尾・STOP／ADDR・TX先読み・AF正常終端・故障復帰・2拒否policyを検査。未使用64行は追加0 B。ACK-discardとdata-NACKはpeerに見える意味が異なり、後者は明示rearmまでreadもNACKする模型。foreign再STARTの反例を残し、実wireは未成立。N03のhost／MMIO link範囲を追加し、次はN04。

- [N03 slave資料の照合](tasks/EXP-06/k0-slave-source-result.ja.md)：固定WCH PDF／SDKの2 fileをbyte数・hash照合。ADDR前のACK、STAR1→STAR2の解除副作用、ACK停止後のSTOPF条件、readのAF終端、他アドレス再STARTの観測不足を対応表へ記録。同じ長さのSDK改変はhash不一致で拒否。workspace3件成功。用途native／ABIの新規実行は0。次は副作用付きregister模型、拒否と復帰の条件付きbackend。

- [N03 K0共有部分](tasks/EXP-06/k0-shared-result.ja.md)：native118件＋workspace3件、ABI132。差分消失と混在snapshotの反例、保護・元のmask復元、容量1／2、10 seed×10,000反復を検査。165 trace・1,203,710操作を保存し、未使用64行は追加0 B。C-bridgeは従来Cと別の通知条件。単一コアIRQ模型の範囲であり、slave register／ACK対応とN03全体は未完了。

- [N02 X035準備の用途接続](tasks/EXP-06/x0-prepared-wire-result.ja.md)：最終native182件＋workspace3件、16実行物・ABI546。840 trace・2,476,232操作で全群一致。全route／packageの可否・open再確認・FaultHeld／abandonと旧結果保持中の再準備・peerを検査。C8 route 0のLTOあり追加RAM16 B、未使用272行は追加0 B。F7はC8 map上のpackage guardで実機fitではなく、非対応32使用行を正常用途の順位へ混ぜない。N02のhost／ABI範囲は実施済み。次はN03。
- [N02 X0用途の受信末尾](tasks/EXP-06/x0-rx-tail-result.ja.md)：native70件＋workspace3件成功。2量×6故障点×5 flagと停止待ち・通知満杯をdirect／event・R/A/B/Cで検査。512 trace・1,667,916操作が独立模型に一致。FIFO排出と有効受信数を区別し、read後のFailureで旧値＋staleを保持。firmware headerと既存68 ABI入力は不変、新規ABI buildは0。次はX035準備との用途接続。
- [N02 X0用途の完了通知](tasks/EXP-06/x0-event-wire-result.ja.md)：native53件＋workspace3件（2 run）・ABI68。1宛先／FIFO1のhintでtakeを許可し、Full・解除・retry・古いhint・peer通信中のhandler／旧takeを検査。160 trace・1,045,096操作が独立模型に一致。PPSは進むが保持された1 packetにより計測／touchが待つ例を保存。LTOあり追加RAM72 B、未使用32行は追加0 B。N02ではRX tail・準備との用途接続を続ける。
- [N02直接回収の用途接続](tasks/EXP-06/x0-register-wire-result.ja.md)：最終native84件＋workspace3件・ABI36、初回36 ELF／imageも不変。272 trace・974,132操作・3,338,336 MMIO／critical eventを保存。5種類のTX途中error、取消、別owner通信中の旧take、pin宣言拒否と公称clock変更を検査。未使用16行は追加0 B。N02全体では通知満杯・RX tail・準備との接続が残る。
- [N01 X0用途とtyped wire](tasks/EXP-06/x0-wire-result.ja.md)：native46件＋workspace3件、正本ABI68（前の入力記録を直した68 ELF／imageも全一致）。表示command/dataと計測pointer/RXを同じR/A/B/Cへ接続。84 trace・743,116操作が独立oracleに一致。未使用32行は追加0 B。実controller初期化・品質・register engine・資源台帳は未検証。次はN02。
- [P4-AUDIT-01 証拠と残作業の監査](tasks/P4-AUDIT-01/result.ja.md)：51 measurement manifestと明示された47移送archiveを照合。D01〜D12、全契約、76候補の索引を作成。IO-04の内部hint callbackと公開callback破棄の違い、全体fit・stack・時間・乱数条件の不足を記録。P4終了や方式採用ではない。次の通常作業はN01の共通wire adapter。
- [P2／EXP-06 X0 device入力照合](tasks/EXP-06/x0-device-result.ja.md)：メーカー2 PDFをhash固定。raw word式各65,536条件、golden4 frameのaddress順、136,200位相の整数模型を照合し、再取得でも一致。header込みwire／合成負荷を計数した。上流ADC設定の136.2 ms模型と28 ms Low継続timeoutを、通常task期限や実測へ読み替えない。workspace3件通過。初期化・実driver・CNVR clear解釈・実機は残る。[課題](tasks/EXP-06/x0-device-exercise.ja.md)は利用者レビュー前。
- [P2／EXP-06 K0上流再現](tasks/EXP-06/k0-upstream-result.ja.md)：固定15 fileから4 ELF、load image 1,348 Bが一致。静的RAM32 B（選択object合計25 B＋整列）、stack／実機は未検証。同梱1,508 B binaryとは不一致。live register10 B・部分反映・循環read・poll入力と固定K0の違いを記録し、workspace3件通過。[課題](tasks/EXP-06/k0-upstream-exercise.ja.md)は利用者レビュー前。
- [P2／EXP-06 X0上流再現](tasks/EXP-06/x0-upstream-result.ja.md)：固定24 fileを再取得し、元source／flagsで4 ELF、load image 4,984 Bが一致。静的RAM180 B、stack／実機は未検証。同梱5,264 B binaryとは不一致。元OLED／INA関数をI2C記録stubへ接続した16入力×2 copyが一致し、workspace3件通過。128×32 vertical・bitbang配線・ACK無視・入力等のU/X0差を一覧化。既存128×64条件と実controller headerは変更せず、[課題](tasks/EXP-06/x0-upstream-exercise.ja.md)は利用者レビュー前。
- [EXP-03 X035準備とcallback](tasks/EXP-03/x035-i2c-event-result.ja.md)：native192件＋workspace3件・ABI134。既存route／setup・Managed・eventを変更せず接続。F7非対応・debug保持・準備失敗・open再検証と、旧結果を保持中の再準備／別owner通信中のcallbackを検査。bool基準4 image不変、LTOあり追加RAM16 B、使用時.data 16 B、未使用64構成の追加0 B。実pin／clock・IRQ・sleepは未検証。[課題](tasks/EXP-03/x035-i2c-event-exercise.ja.md)は利用者レビュー前。
- [EXP-03 資源管理付きI2Cとcallback](tasks/EXP-03/i2c-event-result.ja.md)：native128件＋workspace3件・ABI28。既存headerを変更せず、実験register engine／Domain／完了glue／EventExecutorを接続。周辺返却後と別owner通信中の回収、満杯retry、解除、古いhint、M/S/Uを検証。2発生元の同値tokenを誤配信する反例も保存。配信追加RAM96 B、未使用8構成の追加0 B。実IRQ・Pins準備・sleepと動的source破棄は未検証。[課題](tasks/EXP-03/i2c-event-exercise.ja.md)は利用者レビュー前。
- [EXP-01 I2C回収分離と資源台帳](tasks/EXP-01/i2c-managed-handoff-result.ja.md)：native74件＋workspace3件・ABI28。open／closeにDomainを接続し、旧結果保持中の周辺返却・別owner通信と旧takeを検証。基準8 image不変、LTOあり追加RAM52 B、未使用8構成の追加0 B。実Pins／clock・IRQ共有・event接続は未検証。[課題](tasks/EXP-01/i2c-managed-handoff-exercise.ja.md)は利用者レビュー前。
- [P2-PREP-01](tasks/P2-PREP-01/result.ja.md)：対象・個別ロジック。
- [P2-PREP-02](tasks/P2-PREP-02/result.ja.md)：X0の共有bus・周期・結合ロジック。
- [EXP-02直列単位](tasks/EXP-02/result.ja.md)：データ部品・容量・未使用除去。
- [EXP-02寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：move-only、gate、登録競合、MPSC／SPSCのhost検査とABI比較。
- [EXP-02取得buffer](tasks/EXP-02/acquisition-result.ja.md)：2 blockの生成元／利用側所有、通知と回収、取消と停止、部分生成、入力契約別の負荷集計。[課題](tasks/EXP-02/acquisition-exercise.ja.md)は人による評価前。
- [EXP-03有限work準備](tasks/EXP-03/work-preparation.ja.md)：有限snapshot、再投稿、公平性、M/S/U、非再入と解除。
- [EXP-01 I/O所有model](tasks/EXP-01/io-preparation.ja.md)：受理、取消、sticky終端、世代、end、buffer回収。
- [EXP-03期限model](tasks/EXP-03/timer-preparation.ja.md)：半周・周回・周期集約・未回収通知・停止／再設定。
- [EXP-03宛先結合](tasks/EXP-03/event-preparation.ja.md)：FIFO／集約bit、満杯でもI/O・timer保持、fan-out、配達前解除。
- [EXP-03 完了枠とbus割当て](tasks/EXP-03/completion-arbiter-result.ja.md)：native22件・最終ABI20構成、専用3枠／共有2枠、受理時の予約・停止後のbus返却・takeまでの結果保持、受理公平性の反例。この単位では実driver／event配達は未接続。
- [EXP-03 完了枠と配信の結合](tasks/EXP-03/completion-event-result.ja.md)：native74件。宛先解除でenqueue時ackの再通知が失われる反例、pending保持によるretry、M/S/Uと禁止文脈、end後と古い要求の再検証。追加の[結合費用](tasks/EXP-03/completion-delivery-result.ja.md)はnative76件・ABI28（前回74件を含む）、1宛先／容量1の配信追加RAM52 B、未使用8構成の追加0 B。実driver／ISR・MCU時間は残る。[課題](tasks/EXP-03/completion-event-exercise.ja.md)は人による評価前。
- [EXP-03 metadata fan-out](tasks/EXP-03/completion-fanout-result.ja.md)：native356件＋workspace3件・ABI108。単なるsource token配信の競合回収と、結果コピーの宛先別retryを分けた。停止recipientによる次のsource回収の停滞、copy後のendの意味の変化、追加RAM112〜440 Bと未使用48構成の追加0 Bを確認。[課題](tasks/EXP-03/completion-fanout-exercise.ja.md)は人による評価前。
- [EXP-03 用途側optional](tasks/EXP-03/completion-optional-result.ja.md)：native45件＋workspace3件、45 trace・18,690操作を独立台帳と照合。制御だけをrequiredにし、表示を1回try／最新状態に分けたときの進行・drop・鮮度を比較。共通header7個は不変、実X0とtarget費用は未検証。
- [EXP-06 K0同等比較](tasks/EXP-06/k0-result.ja.md)：同一入力のR/A/B/C、M/S/U、設定容量2の変更課題。
- [EXP-03待機policy](tasks/EXP-03/wait-result.ja.md)：try／非同期／同期、A/B/Cとproducer進行範囲、禁止文脈・期限観測。
- [EXP-03 資源依存](tasks/EXP-03/dependency-result.ja.md)：保有したbus／buffer／lock、既知・未知の依存、同期と明示poll。
- [EXP-06 X0追加比較](tasks/EXP-06/x0-result.ja.md)：R/A/B/C-static-batch。固定1宛先の追加条件を汎用Cと区別。

- [EXP-01 reset／GPIO入口](tasks/EXP-01/runtime-result.ja.md)：vector・data／bss・ctor呼出しの命令検査、raw／class比較。実起動は未確認。
- [EXP-01 clock/time](tasks/EXP-01/clock-time-result.ja.md)：有限clock切替、32／64 bit sample、epoch、変換費用と情報欠落の例。
- [EXP-01 論理時刻](tasks/EXP-01/logical-time-result.ja.md)：上限宣言／推定／native64、端数、既知／不明停止、timerのdomain・半周guard。実sourceの保持・間隔・精度は未検証。
- [EXP-01 I2C master](tasks/EXP-01/i2c-master-result.ja.md)：有限register進行、受信末尾・取消と貸出、容量8／32／64、未使用8条件の追加0 B。実通信・IRQは未検証。
- [EXP-01 I2Cの資源利用権](tasks/EXP-01/i2c-resource-result.ja.md)：native47件・ABI28、宣言PCLK／pin準備と資源台帳、open失敗の返却・end／takeまでの保持・進行停止・古いticket。実clock／pinmux／IRQ adapterは未接続。
- [EXP-01 X035準備adapter](tasks/EXP-01/x035-i2c-setup-result.ja.md)：固定3 PDF・SDK8 file、公称PCLK・gate・PA10／11設定とopen時再確認、native100件・ABI18構成。実配線・電流・clock精度・IRQ共有は未検証。
- [EXP-01 X035 route／package](tasks/EXP-01/x035-i2c-route-result.ja.md)：native136件・ABI68、8設定×C8／F7、非対応pin・debug未解放の拒否、C8 defaultのimage hash不変。物理pin台帳・USB共存・IRQ共有は未検証。
- [EXP-01 I2Cの回収分離](tasks/EXP-01/i2c-handoff-result.ja.md)：native70件＋workspace3件・ABI20。元のMasterを変更せずbackend Viewと外側buffer借用を分け、34条件の全MMIO・byteを照合。backendを回収しても利用者takeまでbufferを保護し、次clientを進める。資源台帳／event／実wire・IRQは未接続。[課題](tasks/EXP-01/i2c-handoff-exercise.ja.md)は人による評価前。
- [EXP-04 媒体・領域準備](tasks/EXP-04/region-result.ja.md)：読出し・program・eraseの粒度、保守的な中断模型、予約とlink／image検査。保存の実機成立・実電断・upload保持は未検証。
- [EXP-04 形式比較](tasks/EXP-04/format-result.ja.md)：RAW／blob／数値key、上書き／2 bank／別単位の完了印。条件付きhost模型であり、実機の保護は未検証。
- [EXP-04 C++試作](tasks/EXP-04/store-result.ja.md)：初回／置換の模型照合、作業buffer・ABI費用と未使用除去。同期走査の読出し回数も記録し、実媒体・所有・走査分割は残す。
- [EXP-04 予算付き読出しと所有](tasks/EXP-04/scan-result.ja.md)：明示pollのbyte上限、共有台帳の重複拒否とticket、結果保持と解放後の無効化。書込み側への統合・ISR／core間共有は残す。
- [EXP-04 走査・所有と更新の統合](tasks/EXP-04/integrated-result.ja.md)：native234件＋workspace3件、既存同期Store272件、新規ABI76と既存読出し76の再検査。受理／key変更のread 0、書込み後の明示再走査、途中失敗・所有喪失、追加RAM80／120 Bを確認。旧272件は初回混在run内の成功部分で、run全体の失敗は記録に残す。[課題](tasks/EXP-04/integrated-exercise.ja.md)は人による評価前。
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
- [EXP-05 H417共有予約](tasks/EXP-05/h417-reservation-result.ja.md)：派生scriptで0／256 B／1 KiB／32 KiBの8 pair、link拒否8条件・実imageの混在拒否3条件・予約検査53件。未使用時にV3F通常RAMへ32 KiBを返却。公開設定とruntime handshakeは未決定。

次は論理時刻の実source・sleep接続とI2Cの周辺準備／所有／IRQ結合、X0の残る変更課題、保存・系列・休止、実共有条件を進める。
P2の実backend待ちで、pureな部品とhost契約の検査を止めない。
