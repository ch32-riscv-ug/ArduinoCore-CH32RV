# sharedとcore-local、実行中と終了待ちの課題案

[N06並列接続結果](h417-transport-result.ja.md)を使う。利用者レビュー前。

1. Shared、View、Endpointの各fieldを共有／core-localに分ける。callback pointerと途中cursorを共有側へ置く問題を説明する。
2. messageのempty、Full、Busy、Closed、Staleを別々に作り、失敗時のoutput保持を確認する。
   reserveのRejectedでまだ区別していない原因も挙げる。
3. callbackをpop後に止め、別threadからrecoverする。新規受理を閉じたこととcallback終了が同じでない理由を説明する。
4. closeだけをBusyにして呼出しから戻す。borrowedを保持した次の呼出しで、handlerを実行せずcloseだけretryすることを確認する。
5. open後・pop後・close前から通常文脈で同じEndpointへ再入する。文脈引数に依存せず拒否し、親permitを保持することを確認する。
   修正前の早期close反例と、実行中flag／終了待ちflagの違いを説明する。
6. 両coreのABI表でlocal RAMとshared RAMを集計する。生成側にないEndpointと、LTOで除去されたViewを確認する。
   shared初期値が両imageにあることから、誰がいつ初期化すべきかをまだ決められない理由を説明する。

hostのmutex／atomicとH417のHSEM／cacheを同一視しない。
変更差分・追加状態の統一評価はN07、人による理解度評価は別工程。
