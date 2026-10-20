# P2 X0共有busと周期処理を結合する作業依頼

通常モデル向け、2026-10-20。P2の次の準備単位。
[比較仕様](../../experiment-spec.ja.md)、[用途](../../workloads.ja.md)、
[固定入力](../../p2-preparation.ja.md)、[前単位の結果](../P2-PREP-01/result.ja.md)を読む。

## 目的と条件

PPS・表示・計測・入力を一つのR対照へ結合し、個別状態機械の検査からX0完成用途のhost検査へ進める。
既存のK0／PPS／Screenと独立oracleを出発点とし、製品coreには接続しない。
単一の通常stack、明示step、静的容量、heapなしの基準を維持する。
この単位も実機接続・書き込みは含めない。

## 実装する範囲

1. 2 tickで終端するfake I2C busを一つだけ用意し、表示と計測が排他的にtransactionを所有する。
   startはbusyを返す基準とし、要求元の再試行順をround-robinで保存する。隠れた待機queueは加えない。
2. 計測は100 tick周期、画面の開始間隔は100 tick以上。遅延後の周期は最新の1回へ集約する。
   画面が使用中のbufferを変えず、完了は通常queueの満杯で失わない。
3. UI状態の変化だけでview revisionを進め、毎巡回で不変の画面をdirtyにしない。
   交渉と表示は希望・送信中・成立を区別する。切断前のqueued Applyを新sessionへ移さない。
4. X-BUS、計測の成功→失敗→復帰、表示中の入力集中、複数のApply、切断と旧終端を一つのtraceで検査する。
5. B2のタッチ取得は50 tick周期・結果8 byteとして別profileへ追加する。
   transaction途中の割り込みを前提にせず、各ownerが進むこと、拒否・欠落・待ち時間を記録する。
6. appを止めてhardwareだけ進める刺激を入れ、終端の保持と回収後の再開を確認する。
   delay／loop外の新しい公開規則は採用せず、明示stepのM profileだけを使う。

時刻付きtraceとbarrier付き故障traceを分け、独立oracle・literalな保存則・busの所有記録で照合する。
markerや観測のためのcounterは費用に識別子を付ける。
fakeのPD処理を実protocolと呼ばず、GPIO・I2Cの実波形を検証した扱いにしない。

## 成果物と完了条件

試作・host検査は `tests/unit/core_redesign/`、ABI確認は既存の `tests/platform/core_redesign/` を拡張する。
入力・trace・glyphの変更は理由を記録し、P1-1と機能差を作らない。
結果はこのディレクトリの `result.ja.md` にまとめ、全体計画とTODOの現在地を更新する。

完了には、共通busの多重所有なし、bufferの不変性、要求の受理・終端・回収の保存則、
周期集約、全ownerの進行、native sanitizer検査、target ABIでの結合ロジックのlink確認が必要。
数値はlogic-onlyと実機を分け、実時間保証や完成firmwareのfitに読み替えない。
起動、K0 slaveのACK/NACK、実PD backend、実IRQ同期、実機設備は次の準備単位へ残す。

通常の不具合はこの担当で修正を続ける。
同じ所有・進行契約でbus共有が成立しない、stack追加や公開実行規則の変更が必要になった場合だけ高位判断へ戻す。
