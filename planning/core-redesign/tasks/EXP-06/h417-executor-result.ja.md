# N06 H417向けFabricと実行器の直列接続

2026-10-22。実験実装 `8f3fc9a`。N06の接続例の追加で、共有並列実装や方式の採用ではない。
[入力](../../h417-executor-inputs.json)、[測定manifest](h417-executor-measurements.json)、
[ABI表](h417-executor-size-matrix.csv)、[変更課題](h417-executor-exercise.ja.md)。
既存Fabric／WorkExecutor／EventExecutorとproduction core・USB・Network・H417並行実装は変更していない。

## 接続条件

[機能別Fabric](../EXP-05/h417-features-result.ja.md)にnormal worker2個を接続した。
Single／Fixed／Sharedの3所有規則、message／blockの有効無効4組、R／A／B／C-notification-bridgeを比較する。
messageは16 B×4件、blockは64 B×2個、domain9。RとAはこの接続では同じ直接進行を使う。
BはworkerごとにWorkExecutor1枠、CはEventExecutor1宛先への集約hintであり、汎用payload配送Cとは別条件。
Singleはcore index0、Fixedは生成0／消費1、Sharedは0／1双方が消費できる。
ここでのindexは模型の所有者識別であり、host threadや実CPUの識別機構ではない。

生成側はFabricへdataを公開してからepoch・種類・宛先を持つticketを返す。
ticketの配送は明示notify／hint、実処理は明示turnで行う。notifyからhandlerをinlineで呼ばない。
公開と通知を別の操作にして、遅れた通知・重複・復旧を検査する。
通知がなくなってもdataが存在する場合は、callerの明示hint再走査で回収できる。
adapterのhintはFabricのsticky notice bitとは別物で、この接続はFabricのnotice bitをackしない。
既存のserial observerはtrace出力だけに使い、実行の可否判定には使わない。

1 callbackはmessageを最大1件popするか、blockのtake／最大8 byte read／releaseのいずれかを行う。
両方がreadyならlaneを交互に選ぶ。8 byteは操作数の上限で、実行時間の上限ではない。
blockは64 B読み終えた次のcallbackでreleaseし、その時点で完了件数とhashを確定する。
M／S／U、Explicit／AfterLoop／Delay、User opt-in、禁止文脈は既存executorの規則を適用する。

## 空とbusy、通知を消す順序

Fabricのbool falseにはemptyとgate取得失敗が含まれる。
この実験のSerialGateは直列callの取得結果を記録し、失敗ならhintを残して次の明示turnでretryする。
role・feature・sessionが有効な場合に限り、gateを取れたfalseをemptyとして扱う。
これは複数thread／coreから読めるcall-local結果APIではない。共有されたentered boolを並列に使ってはならない。
公開APIとしての結果種別や、並列に観測可能なadapter契約は未採用。

正常条件はhintをprobe前に消す。成功なら残量確認のため再度hintを立て、busyでもretryを残す。
emptyの後に新しい公開／hintが入れば、その新hintを保持する。
負の条件はprobe後に消す。emptyを確認した直後に生成側がdata＋hintを公開すると、
新hintを消して、共有queueに1件残したまま処理が止まる反例を保存した。
正常条件ではその1件を配送し終える。負の条件も明示hintで回収でき、data消失とwake hint消失を区別した。

## 停止と復旧

workerのonline=falseは通常callbackの停止であり、実CPUの停止命令ではない。
停止中もmessageの4件、Reading／Ready blockの2個は占有されたまま。生成側は満杯で拒否される。
worker再開後に明示turnで回収するまで、blockは再利用されない。

recoverはFabricの更新・消費を閉じる。producer／consumer役割を持つworkerの停止と、
外部DMA等のquiet宣言をattestへ反映し、全3 bitが揃ってからrestartを許可する。
直列fixtureには実行中callの並列残存がなく、これを実DMA・CPU停止の証拠とはしない。
restartはqueue／poolとadapter hintを消してepochを更新するが、executor内の古いready hintやlocal leaseは自動破棄しない。
古いticketはepoch不一致で拒否し、holding中の古いleaseは次の通常stepで無効化する。
そのcallbackで旧leaseを使って新しいblockをreleaseしない。通常readyはsourceを再確認するhintであり、旧dataへの権限ではない。

8 bit counter感度ではepochの255→0 wrapを拒否した。block generationも255件後は拒否し、
restartでもgenerationが戻らない既存Fabricの条件をそのまま検査した。復旧すれば容量・世代が無限に使えるという保証ではない。

## 検査結果

最終ASan／UBSan runはnative69件＋workspace3件、失敗・skipなし。
284 trace・197,708コマンド、4候補71組で全コマンド後の出力が一致した。

- 3所有規則×4 feature、公開と明示配送、message順序と独立FNV計算、64 B blockの内容・返却。
- message／block take／read／releaseのgate busyと再試行、read予算8 B。
- 消費側停止による容量保持と満杯拒否、読みかけ／返却直前を含む再開。
- late-clear反例、失敗した公開が以前のticketを上書きしないこと。
- 5停止境界でのrecover、未静止attest拒否、古いticket／leaseと新generationの分離。
- M／S／U・domain・opt-in、Setup／Callback／ISR文脈、Sharedの2 consumerとlane交互進行。
- 8 bit epoch／generationの上限、10 seed×100 lifecycleの公開・回収・restartと独立payload hash／件数照合。

独立oracleはpayload hash／順序・完了件数の検査であり、全共有protocolの別emulatorではない。
候補間一致を全体の正しさやP1の全乱数予算達成へ読み替えない。
最終比較は全て直列。既存Fabricのhost並列成功を、この新しいwrapperの並列成功として継承しない。

## ターゲット向け費用

[固定H417配置入力](../../h417-layout-inputs.json)のmarch／ABIを参照し、rv32imafc_zicsr／ilp32fでlinkした。
4候補×3所有規則×4 feature×使用／未使用×LTO有無とempty、計194 build。
使用96行・未使用96行・empty2行で、全ELF flags3、constructor arrayなし、allocatorなし。
未使用96行はemptyと同じimage、アプリ静的RAM0 B。

これはFlash64 KiB／RAM32 KiBのflatな構造的probeであり、V3F／V5Fの実linker・起動・shared配置ではない。
probe_mainはzeroから始まり、V5F固有の配置を再現しない。
1 objectにFabric、Gate、両workerのlocal executorとpointerを含めて測定している。
このobject全体をshared RAMへ置く構成として使えず、shared／core-localの分離は次の検討・実験が必要。

以下はFixed／LTOあり。F0は制御のみ、F1はmessage、F2はblock、F3は両方。

| 候補 | feature | image B | 静的RAM B | アプリobject B | 最大単一.su B |
|---|---|---:|---:|---:|---:|
| R／A | F0 | 920 | 244 | 92 | 64 |
| R／A | F1 | 1,464 | 336 | 176 | 96 |
| R／A | F2 | 2,544 | 520 | 324 | 144 |
| R／A | F3 | 3,056 | 600 | 408 | 144 |
| B-work | F3 | 3,296 | 664 | 472 | 144 |
| C-notification-bridge | F3 | 3,352 | 688 | 496 | 144 |

F0も制御・workerを生成する使用条件なので、0 Bではない。disabled featureの状態は空baseで除去する。
費用にはhash・件数等の観測状態を含み、製品の最小費用を示さない。
probeのharnessは通常192 Bのobjectだが、LTO＋F0ではticket12 Bが除去される。
LTO＋F0／F1では不変のslots32 BがFlashへ移るため、RAMはそれぞれ148／160 B。
ABI表はharness object合計、RAM分、Flash分をsymbolの実アドレスで分けた。
image／静的RAMには整列等も含むため、object合計をそのまま足し引きしない。
単一.suはcall chain・ISR・他コアを含むpeak stackではなく、実runtimeのfitも未確認。

## 再現・履歴と残作業

host：`work/exp06-h417-executor-host-20261022-r2`、XML／log：`work/exp06-h417-executor-tests-20261022-r2.*`。
ABI：`work/exp06-h417-executor-abi-20261022`。分析：`work/exp06-h417-executor-analysis-20261022`。
初回host67件成功、停止中pool保持・公開失敗・generation上限の5件を追加した最終72件も成功。
ABI194件は初回成功。分析の初回はharnessを常に192 Bとする期待で失敗し、
実symbolのticket除去とslotsのFlash配置を確認して集計を修正した。prototype／target buildを期待に合わせて変えていない。
コマンド・入力／tool hash・binary・trace・ELF・map・逆アセンブルと分析履歴をarchiveへ保存した。

N06には、shared transportとcore-local workerの分離、hint／epoch／受付閉鎖の並列保護、
call-local結果の区別を保つhost並列接続が残る。これを高位モデルの採否待ちにせず、通常の比較実験として続ける。
その後の実runtime・両coreの配置／起動はN08、HSEM・cache・IRQ・実DMA停止・reset・peak観測はN09。
公開境界の採否と保証はP5へ渡す。本結果はN06全体やP4の終了ではない。
