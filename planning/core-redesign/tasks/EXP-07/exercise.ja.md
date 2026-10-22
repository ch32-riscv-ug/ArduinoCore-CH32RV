# 変更前に状態と失敗を予測する課題案

[N07統一記録](comparison-result.ja.md)を使う。人による評価は未実施。

1. K0の1件→2件化で、payloadとstaging／read snapshotのどれが増えるかを書く。
   3つの設定frameを処理せず投入し、どれが受理されるかを2つのACK policyごとに予測する。
2. Mono64→Compact32で、frame byte数、chunk、最後の転送量、object全体のRAMを別々に計算する。
   画素配置が変わる部分とPPS・計測・タッチで変わらない部分を挙げる。
3. compile-time touch追加で、buffer以外のperiodic／pending／bus枠を列挙する。
   無効型のlink除去と、使用中instanceから特定機能を取り除くことを区別する。
4. queue4→1で、incrementとapplyを連続投入した結果を予測し、拒否を観測する。
   stepを挟んだ例を試す。lossless retryを足すなら、pending入力・所属session・寿命・RAMを追加して数える。
5. 切断復旧追加はbaseline／変更版の準備中。既存fault testを見ただけで追加機能の課題を完了したとしない。

記録欄は、予測した状態、触ったfile、追加状態、契約の変更、誤解した点、支援回数、完了の可否。
先に正解を見たか、実施順と経験も残す。モデルの生成速度やこの選択差分の行数を学習効果の点数に使わない。
