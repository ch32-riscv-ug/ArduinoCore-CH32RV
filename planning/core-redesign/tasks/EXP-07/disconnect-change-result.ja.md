# E05 X0へ切断復旧を追加する

2026-10-22。実験実装 `1fa8b70`。
[変更前](disconnect-change/before.cpp)・[前のpolicy](disconnect-change/before-policy.hpp)、
[変更後](disconnect-change/after.cpp)・[追加policy](disconnect-change/after-policy.hpp)、
[共通記録](comparison.json)、[ABI表](disconnect-change-size-matrix.csv)。
追加の測定manifestと移送archiveは未作成。以下は実施済みのlocal結果の記録で、別workspaceへの証拠移送は未完了。
production core・USB・LogicCapture・hardware harnessの並行変更は編集していない。

## baselineと追加する能力

beforeは1接続だけを扱う。切断したら受付を閉じ、旧I/Oを停止確認後に回収して安全に停止し続ける。
再接続はUnsupportedで拒否する。壊れたbaselineへ安全性だけを後付けする比較ではない。
afterは再接続を受理し、sessionを更新して古い入力・確定結果を新接続へ適用しない。
旧I/Oのstop witnessと通常回収が終わるまでbufferを再利用せず、それからportを開き直す。

表示・計測・touchを省略せず、既存FixedX0Meaningの本体をPPS型／template引数だけ差し替えて接続した。
集計scriptは元の本文との一致を検査する。R／A／B／C-static-batch、touchなし／あり、入力4件の条件。
message型はbefore4 B、after8 Bで、afterは各入力にsessionを付ける。
CのBatchも実際のmessage型に合わせる。Cを汎用event配信と同じ能力だとは扱わない。

共通準備はconnection_types.hpp、connection_pps.hpp、connection_x0.hppの202行。
IoSlot／IoBufferと、表示・計測・touch／実行器は既存のものを変更せず使う。
この準備と、policy実装へ加えた状態・条件を分けて記録する。

## 変更差分と状態

論理編集fileは2個。選択fileのincludeは1行追加／1行削除、policy本体は14行追加／10行削除。
callbackの所有、物理停止の観測、公開controller APIを利用者がこの行数で実装できるという意味ではない。

追加policyは、最初の接続しか認めないused flagをsession／active_sessionの2 counterへ置き換える。
4つの入力にsession4 Bを追加し、古い入力は通常処理でdiscardする。
transfer開始時のsessionを保持し、旧結果は回収してもvalidへ戻さない。
queueが旧入力で満杯なら、新sessionの入力も通常回収まで拒否される。

queued incrementもsession所属として捨てる条件であり、切断を越えてローカル設定操作を保存する仕様ではない。
peerの再接続だけでapplyを自動再送しない。新しい接続で明示的な確定入力が必要。
epochとIoSlot generationは有限で、無期限の再接続／転送回数を保証しない。縮小counter境界の新規実行はしていない。

## I/Oの停止と通常回収

detachはconnectedをfalse、validをfalseにし、IoSlot.endで新規受理を閉じて取消を要求する。
activeなI/Oとbufferのloanは残る。新attachはlogical peerの存在を示し、portの再開やbuffer返却とは別である。
新sessionの入力を受けても、旧I/Oがquiescentかつterminalになり通常stepでtakeされるまでstartしない。

旧sessionの停止通知は、旧token／active_sessionが一致すれば回収用に受ける。
current sessionと異なるからといって停止通知まで拒否すると旧loanを回収できない。
回収時のvalid判定を分け、旧成功を新接続の成功として見せない。
既にterminalだった成功／失敗、partial failure／cancel、誤token、quiescent=falseも検査した。

M／S／U・domain／opt-inを復旧時も維持し、Userのhandlerを裏で強制実行しない。
禁止文脈ではterminalを保持し、通常の実行機会で回収する。
stop witnessが来なければ無期限に旧loanを保持し得る。時計を進めるだけで返却したことにしない。
その間も独立した表示・計測・touchは明示stepで進められる。WDTや強制resetを実装したものではない。

## 検査

最終ASan／UBSanはnative61件＋workspace3件で64件、失敗・skipなし。
252 trace・217,868コマンド、全4候補63組で各コマンド後の出力が一致した。
独立Pythonのtransfer ledgerを既存whole-application oracleへ接続して照合する。
切断なしの16 traceは元のApplicationOracleとも全application出力が一致した。

- 同じ通常動作、beforeの再接続拒否とafterの復旧。
- 旧I/O停止前の再接続、新要求の保持、buffer書換え拒否、停止通知後もtakeまで保持。
- 既にterminalな旧結果、partial count0／2／8、古いtoken／sessionと新要求の分離。
- 旧入力のdiscard、旧queue満杯、新接続での明示apply、M/S/U・禁止文脈。
- stopがない間の他のX0処理、10 seed×1000選択とdrain。

初回は44成功・2失敗。2件はtest側で全候補のlistを1 traceとしてindexした誤りで、期待の型を修正した。
terminal境界・停止しないbackend・満杯・partial・禁止文脈の18件を追加した最終64件を保存。
prototype／oracleの動作を期待へ合わせて変えていない。

## 費用

V003／X035、LTO有無、4候補×touch有無×before／afterと未使用・emptyで132 build。
使用64行、未使用64行、empty4行。未使用はemptyと同じimage、追加静的RAM0 B。
app object差は20／24 B、harnessと整列込みの静的RAM差は16／24 B、image差は72〜152 Bだった。

X035・LTOあり・touchなしの例。

| 候補 | image before→after B | 静的RAM before→after B | app object before→after B | 最大単一.su before→after B |
|---|---:|---:|---:|---:|
| R | 3,200→3,280 | 484→508 | 360→384 | 112→112 |
| A | 3,232→3,312 | 492→508 | 364→384 | 96→112 |
| B | 3,632→3,736 | 516→540 | 392→412 | 80→80 |
| C-static-batch | 3,616→3,752 | 500→524 | 376→396 | 112→128 |

input payload増加16 Bとcounterのraw量、object整列、link後のRAMを分ける。
Cの一時Batchは24→40 Bになるが、.su最大値は全call chainや実peak stackではない。
共通I/O lease・backend模型・harnessの費用を両版とも含む。E04等の別entryの絶対値と順位付けしない。

## 再現と限界

hostは `work/exp07-disconnect-host-20261022-r2`、XML／logは `work/exp07-disconnect-tests-20261022-r2.*`。
ABIは `work/exp07-disconnect-abi-20261022`、集計は `work/exp07-disconnect-analysis-20261022`。

IoSlotは所有と完了を扱う模型。診断payloadは値＋sessionの8 B、address0x20はfixture値で、USB PD／I2C実protocolではない。
auto_hardwareは宣言したdue5 tickでbackend停止を報告するfixture。holdでそれを止め、明示terminalで観測条件を分ける。
実stop／IRQ／時計・pin・peripheral再初期化・失敗復帰・PPS source・実通信・stack／時間は未検証。
logical peerの切断であり、MCUの電源断やresetを再現していない。
既存UIのconnectedはlogical peerで、backend readyとは区別して読む。

N07の5変更課題の機械比較をそろえた。人の予測・編集・誤解・支援は全件未評価。
現在の作業順は[判断中心の計画](../../decision-focus.ja.md)に従う。
N08へ自動的に進まず、[全体選択案の第三者レビュー](../ARCH-SELECT-01/review-request.ja.md)を優先する。
