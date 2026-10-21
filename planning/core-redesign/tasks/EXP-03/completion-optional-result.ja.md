# 制御だけを待つ構成と、表示への渡し方

2026-10-21。実装commit `725948e`。[入力hash・45 trace・観測値](completion-optional-measurements.json)を保存した。
[fan-out](completion-fanout-result.ja.md)で確認した停止recipientの波及を、用途側の3通りの組み方で比較した。
新しい共通配信器や公開policyは追加していない。既存の実験header7個、製品コア・USBは不変。

## 比較する意味を固定する

sourceは既存のfake Arbiterの1 client・1 record、buffer32 B。produceで4 byteの読出しを終端させる。
publication枠を予約してから結果を1回だけtakeし、sequence・digest・byte数のmetadataへ変換する。
制御consumerはSystem、表示consumerはUser・自動実行opt-inなし。queueは各1枠。

| 条件 | required | 表示が止まっている間 | 表示を再開したとき |
|---|---|---|---|
| all | 制御・表示 | 両者の受取までpublicationを保持し、次のcaptureは待つ | 保持した結果を受け取る |
| once | 制御だけ | 表示へ独立したmetadataコピーを1回try。Full／Staleならdropし、retryしない | queueに受理された古いコピーが残り得る |
| latest | 制御だけ | 用途側の最新状態を置換し、dirtyを保持する。表示queueへは送らない | 表示側が明示的に最新状態を読む |

onceのoptionalコピーはpublication世代の寿命から切り離している。
制御の受取後にpublicationを解放しても、そのコピーを読める。
latestの状態は表示が未登録でも更新し、再登録時に読める。
一方、onceの表示queueは宛先解除で破棄され、解除中の新しいtryもStaleとしてdropする。
両者の「表示に残す情報」は違い、同じ保証の速度比較ではない。

required集合を狭くするだけではoptionalの意味は決まらない。
一度だけtryするのか、最新値を保持するのか、そもそもそのconsumerへ送らないのかを用途が決める必要がある。
今回はallの既存Publicationを変更せず、required maskと用途側の送信・状態保持で3条件を構成した。

## 同じ操作列で観測した違い

produce 1→poll→produce 2→poll→produce 3→pollの後、表示はまだ一度も進めない。
その時点と、最初のdrawで観測した値は次のとおり。

| 条件 | source受理件数 | take件数 | 制御の受取 | buffer | 最初の表示の受取 | loss／置換 |
|---|---:|---:|---|---|---|---|
| all | 2 | 1 | 1 | 2件目の結果とともに借用中 | 1 | 3件目はBusyで未受理 |
| once | 3 | 3 | 1、2、3 | 返却済み | 1 | 表示向け2件をdrop |
| latest | 3 | 3 | 1、2、3 | 返却済み | 3 | 未表示の旧状態2件を置換 |

allで保持されるbufferは、最初のmetadataコピーの元bufferではなく、次の未回収要求のbuffer。
publicationをどこまで待たせるかが、その先のsource受理にも波及する。
onceでは制御を進められる一方、表示の値が最新とは限らない。
latestは最新状態を見る用途に合う意味を持つが、途中の値を全て扱う履歴・command queueの代わりにはならない。
元の受信byte列は、どの条件でもcapture後のmetadataに保持していない。

## 明示的な進行機会

pollは最大1件のcapture、required宛先へのpump、その後にEventExecutorのDelay／Sのturnを行う。
このturnではSystemの制御だけを選び、Userの表示は呼ばない。
drawはExplicit／Uのturnを行い、latest条件では表示が接続中かつdirtyなら用途側の状態を読む。
Explicitなので、この機会に待っている制御handlerが進むこともある。
全required受取後のpublication releaseも、この用途側の処理に明記した。

これはCLIから呼ぶhost操作の意味であり、製品のdelay()やloop外に何かを登録したものではない。
実行時間上限、割り込みbackend、独立stackを追加していない。
制御consumer自体を解除すれば、どの条件でもrequired待ちが残り得る。
表示をoptionalにするだけで、全ての停滞が解消するわけではない。

## Hostの証拠

ASan／UBSanと`-Wall -Wextra -Werror`でnative45件＋workspace3件、計48件が通過した。
全45 trace・18,690操作の全状態を、独立したPythonのsource所有・queue・required受取・latest状態の台帳と照合した。

- 3条件に同じ停止表示の操作列を与え、上表の違いを確認。
- capture前後の制御／表示の解除と再登録：12条件。queuedなrequiredは明示rebindし、optionalのdropや状態保持と区別。
- 制御／表示queueの全満杯集合：12条件。別のqueueのFullとrequired待ちを混同しない。
- 6 seed×1,000生成選択×3条件。解除中のproduce、queue filler、重複attach／detach、無効入力、進行機会不足を含む。
- 最後に明示attach・poll・drawで回収し、全traceでsource受理件数＝take件数、buffer返却、publicationとqueueが空になることを確認。

各consumerでは、sequenceから期待する4 byteのdigestとbyte数を照合する。
observedのdroppedはonceの送信失敗数、removedは解除で捨てたqueue item数（検査用fillerを含む）、
overwrittenはlatestで未表示の旧状態を置換した回数である。
記録用vector・JSON等はhost検査の観測設備で、製品の状態・費用として数えない。
fixtureはtest module内に置き、conftest.pyは追加していない。

初回はharnessの外側class定義中にnested payloadのnothrow trait検査が失敗し、native実行前にsetup45件がerrorとなった。
payload型を外へ分離した再実行が48件通過。既存headerを変更して検査条件を緩めてはいない。

## 再現・範囲

```sh
env EXP03_COMPLETION_OPTIONAL_EVIDENCE=work/exp03-completion-optional-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_optional.py tests/unit/test_workspace.py -q
```

出力先は新しいdirectoryを使う。
local archiveは `work/exp03-completion-optional-20261021-evidence.tar.gz`、234,435 byte、
SHA-256 `7b97865c117ff57d33a4ca94cc120517238bdcd09a3cd7b0248840cc2fe690ac`。
binary・compiler hash／version／flags、入力10 fileのsnapshot、全45呼出しのstdin／stdout／stderr／終了code、
初回と最終のlog／XML、台帳による再照合scriptを保存した。
既存header7個のhashは前回と一致したが、今回の用途構成のFlash／RAM／stackは測っていない。
前回の20 B metadataや複数宛先probeと、今回の12 B metadata・host観測設備を混ぜて費用を推定しない。

この実験の「制御」「表示」はmetadata consumerの役割名で、実USB PD・PPS・renderer・I2Cの検証ではない。
実X0用途への接続、controlを待つ根拠、表示の鮮度・履歴の要件、copy後の取消、実機時間と容量、
best-effort機能を共通で提供するか用途側へ残すかは未決定。
[fan-out教材案](completion-fanout-exercise.ja.md)の変更課題に対する比較材料として使う。
