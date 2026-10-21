# 完了枠とイベント配信の結合

2026-10-21。実装commit `3142cf8`。[測定台帳](completion-event-measurements.json)に入力hashと74シナリオの結果を記録した。
[完了枠・bus割当て](completion-arbiter-result.ja.md)と既存のEventExecutorを、そのまま同じnative executableで接続した。
製品コア・USB・既存の実験headerは変更していない。公開APIと採用policyはP5で判断する。

## 接続と比較条件

専用3 record／共有2 record、利用者3、外部buffer32 byte×3、要求4 byteという条件を使う。
イベント側は1宛先・FIFO容量1。payloadには要求のrecord index・generation・clientだけを入れ、結果本体は移さない。
明示的なpumpがpending通知を調べてsendし、通常文脈のturnがhandlerを呼ぶ。
handlerはpayloadを起動の手がかりとして再検証し、元の要求が現在もnotice可能ならtakeする。
noticeは要求世代・終端・pending・openを確認するため、enqueue後のendと古い要求も除外できる。

| 比較するackの時点 | enqueue後に宛先を解除した場合 | bufferと結果 |
|---|---|---|
| enqueue成功時にack | queueは解除時に破棄され、元のpendingも消えている。再登録しても自動的な再通知の材料がない | take可能なまま残る |
| deliveryでtakeするまでpendingを保持 | queueが破棄されても元のpendingが残り、新しい宛先への明示pumpでretryできる | 配達・takeまで残る |

前者の反例はデータ破壊ではなく、通知による回収の進行が失われる例。
明示cleanupによるtakeは両方で可能であり、検査の最後には全bufferを返却する。
再登録先へ既存要求を配達してよいかはcallerが決めて新しいtargetをpumpに使っている。登録変更に伴う権限移譲の一般policyは提供しない。

後者は汎用的な「exactly once配達」の証明ではない。
この接続ではtakeでpendingも消えるため、既に回収した要求を再びhandlerが処理しない。
複数宛先へのfan-out、複数queue内の重複、instanceをまたぐtokenは対象外。
元の通知を保持しても、callerがpump／turnを呼ばなければ進まず、配達時間の上限もない。

## 確認できた境界

- FIFOが満杯ならsendはFull。元の要求の結果・pending・buffer借用を保持し、先のpayloadを配達した後にretryできる。
- client0の終端を未回収にしたままclient1をdispatch・finishできる。busと結果recordの解放時点が違うことを配信器との結合でも確認した。
- enqueue後のendは、既にqueueにある内部handler呼出し自体を取り消さない。handlerの再検証で利用者の結果処理・takeを抑止し、外側の明示cleanupまでbufferを保持する。
- old要求を明示takeし同じclientを再submitすると、queueに残るold payloadは新しい要求を回収しない。Fullで拒否されていたnew通知を後からretryできる。
- 宛先解除はqueueを破棄し、旧targetへのsendはStale。再登録先は新しいtarget世代で識別する。
- handlerからの再帰turnはReentrantで、さらにhandlerを呼ばない。

M/S/U×Explicit／AfterLoop／Delay×System／User／User opt-inを、両record方式で54シナリオ確認した。
Mの自動機会では全配信を抑止し、SではSystemのみ、UではSystemとopt-in Userを選ぶ。
Explicitでは全domainを選べる。抑止された要求はpending・buffer借用を保ち、後のExplicitで回収する。
未initialize・Setup・Callback・Isrの8シナリオでも、その呼出し中は配信せず、通常文脈に戻って回収できた。
この文脈引数はcallerの宣言であり、実CPUのISR検出や割り込み同期を実装したものではない。

## 証拠と再現

ASan／UBSan、`-Wall -Wextra -Werror`でnative74件＋workspace規則3件、計77件が通過。
内訳は配信許可54、寿命・満杯・再入8、解除とack時点4、禁止文脈8。
fixtureはtest module内に置き、conftest.pyは追加していない。
初回は試作harnessの紛らわしいif字下げが警告errorとなり、nativeを実行せずsetup74件が失敗した。字下げを直した再実行を最終証拠とする。

```sh
env EXP03_COMPLETION_EVENT_EVIDENCE=work/exp03-completion-event-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_event.py tests/unit/test_workspace.py \
  -q --disable-warnings --tb=short
```

出力先は新しいdirectoryを指定する。
local証拠は `work/exp03-completion-event-20261021-evidence.tar.gz`、94,393 byte、
SHA-256 `f15b9b2688aa593389b163c6c425cc40252fd8e9dfd6c7b7ce571923cdebfa5f`。
実行binary、compiler hash／version／flags、使用headerとtestのsnapshot、74呼出しのargs／stdout／stderr／終了code、最終log／XMLと初回失敗XMLを含む。
archive自体はignoredなlocal成果物で、追跡する測定台帳にhashを残す。

この単位で追加したものは結合harnessと検査であり、target向けABI測定は行っていない。
後続の[直接回収との費用比較](completion-delivery-result.ja.md)で、共通接続処理を使うnative76件・ABI28構成を追加した。
既存headerが不変でも、結合全体のFlash／RAM／stackは個別部品の数値から算出しない。
nativeのhandler回数は観測値だが、MCU時間やstack peakの数値ではない。

## 残る比較

実I2C backendの停止・takeと外側完了recordの分離、ISR共有、複数通知先の受理状況・再通知管理、
共有poolの受理公平性、実行器へ接続したtarget ABI費用と用途全体での比較を残す。
今回のpending保持方式と、宛先解除時のfallback回収・明示的な再購読・通知不要の直接pollなどを、用途と費用で比較する。
唯一の共通通知方式として採用したわけではない。
予測と変更の教材案は[配信と回収の課題](completion-event-exercise.ja.md)に分けた。
