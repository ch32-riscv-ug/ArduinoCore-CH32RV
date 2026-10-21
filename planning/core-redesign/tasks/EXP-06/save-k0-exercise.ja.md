# V003保存共存を読む変更課題案

[N05結果](save-k0-result.ja.md)の固定条件を使う。教育効果・人の成績は未評価。

1. 64／128／256 Bの予約、blob8の有効量、1／2 bank、erase回数、保存object208 Bを別々に説明する。
   「1 sector」の語だけで設定せず、64 B fast eraseと1 KiB standard eraseの違いを確認する。
2. rate1 ms・保持1件で保存を1回実行し、100 ms後のpositionを保存なしと比較する。
   保持4件へ変えて、K0内のdeltaや設定FIFOを増やした場合と同じかを検討する。
3. request時の位置−7を保存し、完了前に位置−5へ変更する。保存値と現在値を予測する。
   takeしない場合の次のsaveと、take直後の走査途中のsaveについて理由を述べる。
4. 1 bankの置換中にerase／programを失敗させる。旧値・Empty・Dirty・新値が現れる条件を予測し、dumpとCRCを照合する。
   false returnでも新値が読める例を作る。これを物理的な電断保護保証にできない理由を挙げる。
5. ADDRまたはRX2 byteを保留してstorageを呼ぶ。明示irq前後のsnapshot・FIFO・故障状態を比較する。
   peer timeoutや実ストレッチ時間を知るために、この模型に不足する観測を列挙する。
6. wall-derived msと1 pending tickで、アプリが観測する100 msが同じになるかを確認する。
   WDT、外部入力源、FlashからfetchするIRQ、RAM上の処理は同じ時刻源／実行条件とは限らない。

課題を追加APIの採用条件とはしない。実際の編集差分と追加状態の統一比較はN07、人による理解度評価は別工程。
