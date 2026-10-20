# 生きたI/O leaseと休止準備の失敗

検査日2026-10-20、記録整理2026-10-21。[段階失敗](v006-sleep-features-result.ja.md)で未検査だった、転送中の所有との結合。
runtime試作の動作は変更せず、host刺激と検査を追加した。コア本体・USB・Flash実装を変更する単位ではない。
方式・公開API・回収不能時の方針は採用せず、P5へ渡す材料を記録する。

## 入力と範囲

[入力](../../sleep-lease-fault-inputs.json)は4 byte要求を片方または両方のPortへ受理させた後、休止を要求する。
PrepareのFailed、または新inputで中止する際のAbortのFailed／Waitingを、操作後に注入する。
故障する参加者とleaseを保持する参加者が同じ場合・違う場合、2個の回収順変更を含む。

3一覧方式（明示2個／registry2／4）×timerあり／なし×3故障×8配置・順序×3終端結果＝432条件。
終端は取消要求後の成功4 byte、失敗2 byte、取消2 byteとした。取消要求だけで結果をCancelledへ固定しない。
timerをarmしない同じ刺激で期限構成を比較し、期限機能の有無がこの所有の刺激へ影響しないかを調べた。

Off／Restoreの前にleaseを回収する契約なので、既存leaseを保持したままその段階へ進む刺激は正しい流れではない。
今回の複合刺激はPrepare／Abortへ置き、後段の障害は前の156条件と分ける。
実DMAが止まらない場合、callbackが新規転送を始める場合、ISR／core間共有はこの直列模型の対象外。

## 操作と観測

| 境界 | 観測した所有と受理 |
|---|---|
| 準備／中止が失敗 | Fault、一覧freeze、全参加者の新規受理を閉じる。実行中leaseへcancelを要求する |
| 停止確認前のretry＋poll | Faultへ戻り、bufferを空きにしない。実行中／未回収の所有は変わらない |
| 停止宣言なしの終端・違うticketの終端／take | 拒否し、結果と所有を変えない。所有中bufferのread／writeも拒否する |
| 正しい終端を記録、まだtakeしない | terminalでもleaseは残る。retry＋poll後もFault、受理閉鎖を保つ |
| 両方のうち片方をtake | 回収したbufferは空くが、相手のleaseが残る間はretry＋poll後もFault。回収側も新規受理を閉じたまま |
| 最後の結果をtakeしてretry＋poll | Activeへ戻りfreeze解除。全bufferを空きにし、新規受理を開く |
| 同じPortへ次の要求を受理 | ticketを2へ進める。以前のticket1の終端／takeは拒否し、新しいleaseを解放しない |

ここでの「停止宣言」はfake adapterのquiescent引数であり、実hardware停止を測った証拠ではない。
hardwareが同じmemoryへ後からアクセスしないという実driverの根拠は別途必要になる。
callbackの途中の副作用を原子的に巻き戻したものでもない。表は各呼出しが戻った境界である。

Abortを誘発したinputは、修復後も明示takeまで残った。
部分終端はtransferred=2と先頭の生成値を確認した。takeでbufferが返ったことは、未転送の末尾まで新しいdataになった意味ではない。
利用者は結果と転送長を確認し、今回の有効範囲を選んで処理する必要がある。

すべての条件でsleep入口は呼ばず、論理clockはRunningのままだった。
この刺激で意図的に試したretry＋pollは1 leaseで3回、2 leaseで5回。未回収の境界で試した分を含み、必要最小回数や性能値ではない。
実行中のI/Oが進むのをretryが代行したわけではなく、終端とtakeは各々明示的に与えた。

## Hostの証拠と再現

追加432件、既存の段階失敗156件、workspace規則3件、合計591件が通過した。
ASan／UBSan、警告error、assert有効のnative binaryを保存し、432条件をそのbinaryで再生した。
tests配下にconftest.pyは追加していない。

同じ故障・配置・結果ごとに、3一覧方式×2期限構成の6 traceがbyte単位で一致した。
その72群を[集計表](v006-sleep-lease-fault-matrix.csv)へ、全432組の引数と全snapshotをarchiveへ保存した。
phaseだけでなく、所有・空き・取消・終端・ticket・回収結果・転送長・先頭read値を比較する。

runtimeのcoordinator・I/O所有・論理clock・期限とABI入口の5入力は、[直前の測定](v006-sleep-features-result.ja.md)からhashが変わっていないことを確認した。
本単位で新しくABIを測ったものではなく、前の64 ELFを新しい測定件数へ加算しない。
故障注入用のhost wrapper・出力・観測storageをコアのRAM費用ともみなさない。
[再現記録](v006-sleep-lease-fault-measurements.json)にnative binary・入力・snapshot・archiveのhashを残す。

## 判断へ渡すことと未検証

現試作ではretryを受理しても、未回収leaseによりActivateが成立せず、pollで再びFaultになる。
これをFaultとして示すか、通常の回収待ちとして別に示すかは使い勝手・診断の判断点。
最後の明示回収まで保持する不変条件と、状態の公開の仕方を分けてP5で検討する。

止まらない実I/Oの回収不能、driverの部分的な停止・再設定、通知の配達、資源依存、登録一覧の完全性、実V006復帰・電流は未検証。
有限432条件は割込みの全命令間やすべての障害組合せの網羅ではない。
時間保証・stack peak・動作速度を測ったものではなく、回収の進まない場合に自動timeoutやresetを追加したものでもない。
[順序を予測する課題](v006-sleep-lease-fault-exercise.ja.md)は利用者による評価前である。
