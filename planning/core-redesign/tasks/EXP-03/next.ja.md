# EXP-03 継続単位

2026-10-20。通常モデルで続ける。[全作業の状態](../../execution-status.ja.md)も維持する。
製品の方式・公開APIの採用はP5に残す。host／ABIの成功を実機成立へ置き換えない。

## 独立して検査済みの部分

- [I/O所有model](../EXP-01/io-preparation.ja.md)：受理、sticky終端、取消要求と停止、buffer回収、endと世代。
- [有限work](work-preparation.ja.md)：snapshot、一巡件数、公平性、M/S/U、非再入。
- [期限model](timer-preparation.ja.md)：半周、周回、周期集約、通知保持、stop／再設定。
- [宛先結合](event-preparation.ja.md)：payloadと集約通知、満杯時のI/O／timer保持、fan-out、配達前解除。
- [待機policy](wait-result.ja.md)：try／非同期／同期、producerと許可pump、禁止文脈、期限観測。
- [資源依存](dependency-result.ja.md)：bus／buffer／lockを保有した待機、既知の阻害と不明、非同期での明示返却。
- [K0同等用途](../EXP-06/k0-result.ja.md)：R/A/B/C、M/S/U、設定容量2の変更課題。

## 次の通常作業

1. [X0のcompile時除去](../EXP-06/x0-features-result.ja.md)の後続として、表示交換・独立library等の用途変更課題を比較する。
   固定off／onの比較と旧runtime条件を分け、固定型で実行時切替までできたことにしない。
   C-static-batchは固定1宛先であり、動的登録を持つ汎用Cと能力を揃えた比較へ読み替えない。
   実I/Oへの接続、資源ごとのsystem／user分類、周回時計・sleepも別に検査する。
2. [資源依存の模型](dependency-result.ja.md)を実I/O・所有台帳へ接続する。
   [I2C masterのregister試作](../EXP-01/i2c-master-result.ja.md)はhost／linkで検査済みだが、clock／pin準備、IRQ保護、資源台帳との接続はまだ行っていない。
   模型の所有・依存情報が実backendでも取得できるか、snapshotの同期費用を確認する。
   能力の宣言と検出可能な事実を分け、未知を拒否する比較条件を公開APIの採用へ読み替えない。
3. 完了枠予約と固定arbiterを基準のbusy方式から分離して比較する。
   end／取消／回収・古い通知、fan-out途中拒否を維持し、通常mailbox満杯で終端を失わない。
4. 実ISR投稿の保護policy、producerの複数性、bufferとpoolの共有を接続する。
   単一コアIRQ禁止をコア間lockの代用にしない。型番・実clock・backendが未確認ならhostの値で埋めない。
5. 複数timer、tick／次期限方式、時計変更とsleepの接続を比較する。
   [論理時刻とtimer境界](../EXP-01/logical-time-result.ja.md)では端数とdomain変更・半周guardを模型で確認したが、物理の観測上限・保持・精度は確定していない。
   32 bitの半周を超える停止は、広い時計・復帰時再基準化・保証外の明示を別条件で扱う。
6. 独立stackのport・静的容量と深さ制限付き再入を基準から分けて調べる。
   固定版・target適合・stack／制御情報を記録する。成立する場合だけX035以上で実測し、host値で代用しない。

正しさが通った構成からABIの容量・初期化表・symbol・stack情報を取得する。
実cycle、IRQ禁止時間、stack peak、実protocolには実機の証拠が必要。
[EXP-01の論理時刻](../EXP-01/logical-time-result.ja.md)の実source接続とI2Cの周辺準備／所有結合、EXP-04保存、
EXP-05系列／拡張、休止・取得bufferも独立して進める。
これはagentの並列起動を要求する意味ではない。
公開文脈・所有・寿命の比較契約を変える必要があるときだけ高位判断へ戻す。
