# EXP-03 継続単位

2026-10-20。通常モデルで続ける。[全作業の状態](../../execution-status.ja.md)も維持する。
製品の方式・公開APIの採用はP5に残す。host／ABIの成功を実機成立へ置き換えない。

## 独立して検査済みの部分

- [I/O所有model](../EXP-01/io-preparation.ja.md)：受理、sticky終端、取消要求と停止、buffer回収、endと世代。
- [有限work](work-preparation.ja.md)：snapshot、一巡件数、公平性、M/S/U、非再入。
- [期限model](timer-preparation.ja.md)：半周、周回、周期集約、通知保持、stop／再設定。
- [宛先結合](event-preparation.ja.md)：payloadと集約通知、満杯時のI/O／timer保持、fan-out、配達前解除。
- [待機policy](wait-result.ja.md)：try／非同期／同期、producerと許可pump、禁止文脈、期限観測。
- [K0同等用途](../EXP-06/k0-result.ja.md)：R/A/B/C、M/S/U、設定容量2の変更課題。

## 次の通常作業

1. X0に同じI/O・データ・時刻・rendererを接続してR/A/B/Cを比較する。
   input、PD、計測、表示、touchのstorageとmetadataを数え、宛先FIFOと入力queueを二重計上しない。
   同じ進行機会と外部入力で受理・拒否・出力・回収を照合し、候補だけに有利な処理量を与えない。
2. 資源を保持した待機を追加する。waiterがbus／buffer／lockを保有し、producerに同じ資源が必要な条件、
   producerが未参加userである条件、callback内からの禁止された待機を検査する。
   循環待ちをtimeoutで抜けたことを成功にしない。能力の宣言と、実backendで検出できる事実を分ける。
3. 完了枠予約と固定arbiterを基準のbusy方式から分離して比較する。
   end／取消／回収・古い通知、fan-out途中拒否を維持し、通常mailbox満杯で終端を失わない。
4. 実ISR投稿の保護policy、producerの複数性、bufferとpoolの共有を接続する。
   単一コアIRQ禁止をコア間lockの代用にしない。型番・実clock・backendが未確認ならhostの値で埋めない。
5. 複数timer、tick／次期限方式、時計変更とsleepの接続を比較する。
   32 bitの半周を超える停止は、広い時計・復帰時再基準化・保証外の明示を別条件で扱う。
6. 独立stackのport・静的容量と深さ制限付き再入を基準から分けて調べる。
   固定版・target適合・stack／制御情報を記録する。成立する場合だけX035以上で実測し、host値で代用しない。

正しさが通った構成からABIの容量・初期化表・symbol・stack情報を取得する。
実cycle、IRQ禁止時間、stack peak、実protocolには実機の証拠が必要。
EXP-01のruntime・物理backend、EXP-04保存、EXP-05系列／拡張、休止・取得bufferも独立して進める。
これはagentの並列起動を要求する意味ではない。
公開文脈・所有・寿命の比較契約を変える必要があるときだけ高位判断へ戻す。
