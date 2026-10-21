# 変数をobjectに残すかstackに残すか

[共通workflow比較](two-stack-result.ja.md)を使う教材案。人による評価前で、公開APIではない。

1. 入力を受けて要求を出し、完了が来ないまま1,000 turn進むとき、各方式で要求値がどこへ残るか予測する。
2. turnなしで入力を2件与える。slotの保持値、拒否数、勝手な処理が進むかを予測する。
3. 継続state8 Bとcontext128 B＋stack512 Bを、表のRAM差へ対応させる。整列を忘れない。
4. host `ucontext`、整数命令模型、実MCUの3種類で確認できることを分ける。
5. IRQや別コアからswapした場合に、この実験のどの前提を外れるか説明する。
6. local変数がRAII所有者だった場合、suspend中に破棄してよいと今回の結果から言えるか検討する。
7. FreeRTOSが入力1件目でtaskをwakeする場合、burst2件の意味が同じになるfixture条件を考える。

実行traceと予測の差を記録する。模型の成功や予約配列量を、安全なstack容量・実切替時間へ読み替えない。
