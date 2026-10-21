# 休止前の残作業と実行機会を読む課題案

[N06休止接続結果](sleep-work-result.ja.md)を使う。人による理解度評価は未実施。

1. job3 stepとI/O1件を開始してrequestする。cancel受理／quiescent終端／worker回収／job完了を別々に進め、
   Armedへ移れる時点を予測する。pollだけを繰り返してもcallbackが呼ばれないことを確認する。
2. User jobを残し、Delay／Sでturnする。同じjobをExplicit、またはopt-in＋Uで進める場合と比較する。
   休止準備中の例外としてUser処理を裏から実行すると、何の契約を変えることになるかを説明する。
3. Prepare中に入力をpostしてabortさせる。残jobと取消要求が消えていないことを確認し、
   旧I/Oの終端回収後に新I/Oを始める。入力通知の回収とI/Oの回収を混同しない。
4. Sleepingから片側だけReadyにする。clock復元前／復元後／両側Restore後の新規受理を比較する。
   known／unknown復帰で既存Timerの期限通知がどう変わるかを予測する。
5. 負の比較CheckJobs=falseでjobを3 step残してenterする。I/OだけのQuietが不足する理由と、
   登録を忘れた別workerやDMAまで自動で発見できるかを分けて述べる。
6. タイマーを無効にしてABI表を比較する。empty-baseによるobject配置差、clockが残る理由、
   未使用objectが0 Bになることと登録済みRegistry枠が無料でないことを区別する。

課題の編集差分・追加状態の統一記録はN07、人の成績は別工程。
この実験に実際のsleep命令を足すだけでは、atomic wake gate・相手I/O・電流の検証は完了しない。
