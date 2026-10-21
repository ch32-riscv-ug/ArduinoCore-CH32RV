# 同じ値を与えても同じ比較にはならない

[共通観測境界の比較](kernel-trace-result.ja.md)を使う教材案。人による評価前。

1. 入力1件でtaskがwakeし、その間にqueueが空く場合と、batchの最後まで処理を止める場合で、2件目が受理されるか予測する。
2. controllerよりworkerの優先度を高くした場合、今回のbatch契約が保たれると言えるか説明する。
3. 2 taskと3 taskで、完了なしに保持できる入力を全てのqueue・localへ配置する。
4. 追加taskのstack量だけ比較すると、どの機能差を取り落とすか説明する。
5. `drain`で未到着完了を作らないこと、処理が止まること、処理が完了したことを区別する。
6. 固定kernelのhost実行と、WCH portの実切替で何が共通し何が未検証かを列挙する。
7. controller／idle／IRQ領域を含むfixtureの総量と、製品用途の必要量を区別する。

予測・trace・解釈を記録する。hostの処理時間やstackをMCU値へ転用しない。
