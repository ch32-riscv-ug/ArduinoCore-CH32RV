# N04 有限の依存処理と再入深さ1／2／4

2026-10-22。実装commit `56af967`。
[入力](../../reentry-depth-inputs.json)に、actor5個・資源2個の人工例と比較条件を固定した。
既存実行器・wait policy・製品コアは変更していない。方式や公開APIの採用ではない。

## 比較した呼出し構造

継続型は依存先の完了をtryし、未完ならPendingで戻る。次の明示roundで再試行する。
入れ子型は依存先のsystem処理へ直接入り、呼出し元の関数frameを保持する。
許可するcallback深さを1／2／4とし、同じactive actorへ入り直さない。
入れ子userは参加済みでも禁止し、外部IRQ／別コアの完了は明示刺激でだけ与える。
外部producerを通常callbackとして実行したことにはしない。

一回の依存呼出しは有限で、失敗時も戻る。無期限のwait loopや、待っている間に任意の登録処理を
進めるschedulerを実装したものではない。ここでの入れ子は別条件の内部経路であり、
既存の公開callback入口や同期wrapperの禁止を変更しない。
[従来wait比較](wait-result.ja.md)のtimeout／producer条件を置き換えない。

呼出し中に取得した資源は待機中も保持し、unwindで自分の保持だけを返す。
外部ownerと不明ownerは勝手に解放しない。cycleの自動解消や優先度継承はない。

## 観測した差

| 刺激 | 継続型 | 入れ子型 |
|---|---|---|
| 長さ1〜5の依存列 | 一回では先へ入らずPending。明示roundで完了へ進む | 許可深さ以内なら一回で完了。それ以上はDepthLimitでunwind |
| 自己依存・循環 | 明示再試行しても循環部分は未完 | activeに達すればActive。それより先に制限に達するとDepthLimit |
| 呼出し元がproducerの必要資源を保持 | 戻った後で保持を解消して進める余地がある | 深さを増やしても保持は消えない。HeldByActive、または先行するDepthLimitで戻る |
| IRQ／別コアの完了待ち | 外部刺激前はPending | 同じ。外部完了に別の通常callback frameは要らない |
| 自動範囲M／S／U | 許可範囲外はScopeExcluded | 深く呼べても範囲を広げない。入れ子userは別途禁止 |

深さ制限は依存の循環を解決する機能ではない。制限で戻ったことをtimeout成功や処理完了へ変換しない。
明示roundはactor0〜4を各一回試す固定操作で、全体として最大5回のcallbackを試す。
公平性・任意のtask集合・実時間上限を保証したものではない。

## 検査

最終native78件＋workspace3件、計81件成功。既存plugin由来warning162件。
137 trace・864コマンドを保存した。

- 深さ1／2／4、長さ1〜5の依存列について、単一試行の結果・完了集合・callback数・深さを照合。
- 長さ1／2／3／5のcycle、active ancestorの資源保持、不明／他owner、禁止文脈、不正graphを検査。
- IRQ／別コアの許可・停止、user参加、M／S／Uと入れ子禁止を検査。
- seed 0〜9で各1個の非循環graphを作り、4構造で根からの独立な到達列と結果を照合。
  5回の明示round後に全actorが完了することを確認した。

乱数は小さい10 graphであり、大量の乱数列やP1の操作数条件を満たしたと数えない。
逐次模型の結果で、実IRQ共有・atomic・リアルタイム性の検証ではない。
各コマンド終了時にactiveとdepthが0へ戻ることはhost driverでもassertする。

初回は74件成功・1件失敗。深さ1では資源検査の前にDepthLimitとなる実装に対し、
検査が全深さでHeldByActiveを要求していた。実装の判定順を維持し、期待結果を修正した。
不正graph・停止外部producerの6条件を追加し、全81件を再実行した。初回成功数は加算しない。

## ABIと関数frame

[36構成](reentry-depth-size-matrix.csv)は2 ABI×LTO有無×4構造の使用／未使用＋empty。
未使用16行は追加image／静的RAM／用途objectが0 B、empty binaryと一致した。
allocator・constructor arrayは検出されなかった。初回の集計欄に以前のprobe由来の
`protected`等の名称が残っていたため整理し、36構成を再生成した。全36 imageは初回と一致。
最終記録では`nested`と`depth_limit`で条件を表す。

V003、LTOなし、単位B。用途object96 B、共通harness68 B、静的RAM合計164 Bは全構造で共通。

| 構造 | Flash image | 最大単一frame | probeの特定呼出し経路の解析値 |
|---|---:|---:|---:|
| 継続型・深さ1 | 1208 | 28 | 32 |
| 入れ子・深さ1 | 1160 | 28 | 60 |
| 入れ子・深さ2 | 1176 | 28 | 76 |
| 入れ子・深さ4 | 1176 | 28 | 108 |

入れ子の`invoke`は16 B、probe入口は28 Bのframeを持つ。
逆アセンブルでは、未完の依存先へ再帰callし、制限やactiveで戻るcalleeにもframeが存在する。
許可callback数Lに対し、失敗する一段を含む最大L+1個の`invoke` frameを数えた。
表の解析値は`28 + 16×(L+1)`、継続型は`28 + 4`。
X035の同じLTOなしprobeでは入口64 B、入れ子16 B、継続型invokeは0 Bで、
同じ解析値は継続64 B／入れ子96・112・144 B。

これは生成コードの特定経路の算定で、実行して観測したpeakでも、全runtimeのstack予算でもない。
IRQ、他の呼出し、実callbackの局所変数、別compiler、LTOの変換は別に扱う。
depthが一定でもcallback内容でstackは変わり、このtoyの値を製品のstack必要量へ転用しない。
静的RAMが同じことや`.su`最大値だけで、深さを増やす費用が0とは判断しない。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_reentry_depth.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_reentry_depth.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-depth-abi>
```

`EXP03_DEPTH_EVIDENCE`に未作成directoryを指定すると入力・実行物・traceを保存する。
[記録](reentry-depth-measurements.json)にJUnit・tool・入力hash・ELF・解析・archiveを保存した。
[教材案](reentry-depth-exercise.ja.md)は人の評価前。
N04の深さ比較を実施した。別stackの固定port・静的配置・同じ仮想I/O列の対照は未完了。
2 stack協調方式とtaskごとのstackを持つ方式は区別し、portの不適合をhost値で埋めない。
次はX035以上のport適合と最小配置を確認する。P5の方式採用やN04全体の終了ではない。
