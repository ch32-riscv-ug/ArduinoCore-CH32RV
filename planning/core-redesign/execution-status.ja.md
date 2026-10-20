# P4までの実行状況

2026-10-20。利用者の指示は「P4までで進められるところを全て進め、レビュー・高位判断が必要なところを残す」。
実行方式の採用はP5で扱う。通常の実装修正・資料照合・試作・測定はこのセッションで続け、作業単位ごとにコミットする。
この一覧は完了監査にも使い、hostの成功で実機・利用者評価を埋めない。

| 領域 | 現在の証拠 | 進める作業 | 実機・レビュー等が残る部分 |
|---|---|---|---|
| P2 再現 | EXP-00、36 build一致 | 測定の拡張時も入力と生成物を記録 | 実runtimeとは別 |
| P2 用途対照 | PREP-01と02、用途のhost／ABI。[独立runtime ELF](tasks/EXP-01/runtime-result.ja.md) | 実起動・周波数と精度、I/O backend、上流のbuild専用再現 | 実IRQ共有、wire、PD、実機起動 |
| EXP-01 最小構成・I/O | [I/O所有model](tasks/EXP-01/io-preparation.ja.md)：host22件、ABI48構成。[reset／GPIO入口](tasks/EXP-01/runtime-result.ja.md)：host16件・ELF40・拒否link12・初期化故障12。[clock/time](tasks/EXP-01/clock-time-result.ja.md)：host34件・ELF40。[I2C master](tasks/EXP-01/i2c-master-result.ja.md)：host59件・ELF36、貸出・受信末尾・取消 | 論理時刻の伸長と不連続、I2Cのclock／pin／所有結合、ISRと資源引渡し・回収の接続 | 実起動・周波数・精度・周辺・IRQ時間 |
| EXP-02 データと同期 | [直列単位](tasks/EXP-02/result.ja.md)と[寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：host92＋31件、link200＋68構成、未使用36＋16行が0 B、TSan4条件 | 実行器への待機接続、ring／message／poolの共有条件 | 実ISR／コア間の同期と禁止時間 |
| EXP-03 実行・通知 | [work](tasks/EXP-03/work-preparation.ja.md)・[期限](tasks/EXP-03/timer-preparation.ja.md)・[宛先とI/O結合](tasks/EXP-03/event-preparation.ja.md)・[待機policy](tasks/EXP-03/wait-result.ja.md)・[資源依存](tasks/EXP-03/dependency-result.ja.md)：host151件、ABI212構成 | X0の変更課題、依存台帳の実backend接続、IRQ共有、完了枠／arbiter、別stack対照 | MCU時間・stack観測、採否はP5 |
| EXP-04 保存 | V003／X035の消去方式の資料確認 | read／program条件、媒体model、RAW／blob／key、1／2／4単位、領域・linker・設定の検査 | 実電断・停止・upload保持 |
| EXP-05 拡張 | タッチ追加のR対照 | 表示交換・独立library・能力差・H417所有modelと資料確認 | 実cache可視性・両コア・reset |
| EXP-06 完成用途 | [K0 R/A/B/Cと容量2変更](tasks/EXP-06/k0-result.ja.md)：host82件、ABI116構成。[X0追加比較](tasks/EXP-06/x0-result.ja.md)：host55件、ABI116構成、同じ進行機会・故障・50Hzタッチ。[X0機能除去](tasks/EXP-06/x0-features-result.ja.md)：host88件・ABI116構成 | 表示交換・独立library等の変更課題、K0の物理接続 | 実通信・電流・PPS source・教材の利用者レビュー |
| V006休止 | 未実施 | 保持条件の資料確認、fake timeとwake競合、休止調停の比較 | 復帰実機・静止電流 |
| 取得buffer | 未実施 | 2 blockの所有と返却、生成量・consumer停止をmodel化 | 実ADC/DMA、欠落と時間 |
| P4教育 | 教材案、[X0機能除去](tasks/EXP-06/x0-feature-exercise.ja.md)、[時刻読出し](tasks/EXP-01/time-read-exercise.ja.md)、[I2Cの貸出課題](tasks/EXP-01/i2c-loan-exercise.ja.md) | 残る変更課題の教材化、変更差分・必要状態の比較 | 人が予測・変更した結果のレビュー |

不足する設備や上位判断は該当部分の未検証として残し、独立した作業を続ける。
型番・資料・相手・観測がない項目を、既存fixtureの存在やcompile成功だけで実施可能と決めない。
実機操作を始める場合は既存の明示fixture・identity・lock・終了条件を使い、接続先を推測しない。

## 最新の完了単位

- [P2-PREP-01](tasks/P2-PREP-01/result.ja.md)：対象・個別ロジック。
- [P2-PREP-02](tasks/P2-PREP-02/result.ja.md)：X0の共有bus・周期・結合ロジック。
- [EXP-02直列単位](tasks/EXP-02/result.ja.md)：データ部品・容量・未使用除去。
- [EXP-02寿命・共有単位](tasks/EXP-02/shared-result.ja.md)：move-only、gate、登録競合、MPSC／SPSCのhost検査とABI比較。
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
- [EXP-01 I2C master](tasks/EXP-01/i2c-master-result.ja.md)：有限register進行、受信末尾・取消と貸出、容量8／32／64、未使用8条件の追加0 B。実通信・IRQは未検証。
- [EXP-06 X0機能除去](tasks/EXP-06/x0-features-result.ja.md)：固定off／on・専用対照・旧runtime比較と変更課題。

次は論理時刻の伸長・clock/sleep不連続とI2Cの周辺準備／所有／IRQ結合、X0の残る変更課題、保存・系列・休止、実共有条件を進める。
P2の実backend待ちで、pureな部品とhost契約の検査を止めない。
