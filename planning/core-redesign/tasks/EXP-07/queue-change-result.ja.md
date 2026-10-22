# E04 X0入力queueを4件から1件へ減らす

2026-10-22。実験実装 `ab5913a`。
[変更前](queue-change/before.cpp)、[変更後](queue-change/after.cpp)、[ABI表](queue-change-size-matrix.csv)、
[変更課題の共通記録](comparison.json)、[共通測定manifest](change-exercises-measurements.json)。
既存FixedX0Meaning／X0Pps／実行器／production coreは変更せず、実験の容量選択用型を追加した。

## 差分と準備の費用

論理上のexercise.cppは1 file、容量4→1の1行差分。
この選択を可能にする共通準備としてx0_capacity.hppを追加した。Rは固定array、A／BはQueue、CはStaticMailboxを容量化する。
Cの既存adapterはBatch型が容量4へ固定されているため、Nに対応する接続を別の特殊化へ置いた。
この準備を「利用者の1行変更」と同じ費用に含めず隠すことはしない。共通記録にfile・byte数・行数を残す。
同じ意味の本体・PPS session・periodic・bus所有は既存のものを使う。

before／afterは同じ新しい選択層で比較し、さらに元のFixedX0容量4をoriginal controlとして同じtarget entryでbuildした。
32組でoriginalとbeforeのimage量・静的RAM・app objectが一致し、28組はimage byteも一致した。
残る4組はC・LTOなしで、サイズは同じだがbyteは異なる。元のbinary全てを再現したとは扱わない。

## 意味の変化

reject-newの規則を変えず、burst容量だけを減らした。
`attach → key +1 → key apply → step`では、4件版は2入力を受理して5020 mVを確定できる。
1件版はapplyを拒否し、desired5020のままPD requestを開始しない。
capacity縮小だけで同じ入力列の意味が保たれると仮定しない。

`key +1 → step → key apply → step`と明示的に間を空けた入力列は、両容量で全出力が一致した。
これはcallを詰めない例であり、residentなlossless retry producerを実装・計測した結果ではない。
retryを作るならpending入力とその所属session、閉鎖・取消等の状態を別に数える。
kindだけを保留して再接続後にenqueueすれば、新しいsessionへ意図を移してしまう可能性も比較対象になる。
M／S／Uで処理を抑制した機会は、入力queueの空きを作らない。

## 検査

新規ASan／UBSanはnative39件＋workspace3件で42件成功。164 trace・209,172コマンド。
同じ条件の全4候補41組が一致し、各traceを独立Python oracleへ照合した。
容量1は既存ledger modelの受理境界だけへ射影し、入力拒否・PPS session・完了・表示・計測・touchを比較する。
新prototypeのqueue処理をそのままoracleへ写したものではない。

burstによるapply拒否、入力の明示的な間隔、domain／opt-inと進行抑制、切断・古い完了、10 seed×1000選択を検査。
借用中bufferのcorruptionは0、start／terminal／回収の件数関係も検査した。
新しいqueue容量の実験であり、物理PD・I2C・実IRQ・起動・実時間の検証ではない。

## 費用

V003／X035、LTO有無、4候補×touch有無×before／after／originalと未使用、emptyで196 build成功。
主比較64使用行、original32行、未使用96行、empty4行。未使用96行は追加静的RAM0 B、emptyと同じimage。
全32組でapp objectと静的RAMは24 B減った。8 B envelopeを3件減らした分に相当する。

X035・LTOあり・touchなしの例。

| 候補 | image before→after B | 静的RAM before→after B | app object before→after B | 最大単一.su before→after B |
|---|---:|---:|---:|---:|
| R | 2,528→2,472 | 412→388 | 328→304 | 96→96 |
| A | 2,552→2,496 | 412→388 | 328→304 | 96→96 |
| B | 3,024→2,968 | 444→420 | 356→332 | 80→80 |
| C-static-batch | 2,960→2,864 | 428→404 | 340→316 | 144→128 |

Cの一時Batchは40→16 Bになるが、最大単一.suは16 B減という別の値。
static RAM、sourceの一時領域、関数frame、実測peak stackを混同しない。実peakは未観測。
image／RAMにはharnessを含む。同じentryでの差であり、Arduino製品全体の最小費用ではない。

## 再現と残る部分

host：`work/exp07-queue-change-host-20261022`、XML：`work/exp07-queue-change-tests-20261022.xml`。
ABI：`work/exp07-queue-change-abi-20261022`。集計：`work/exp07-change-analysis-20261022/analyze_queue.py`。
本体・新機能の追加ではなく機械的な変更比較。実機、resident retry producerの総予算、利用者の予測／編集評価は未実施。
