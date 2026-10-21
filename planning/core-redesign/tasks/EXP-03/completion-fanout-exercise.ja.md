# 一つの結果を複数箇所へ渡すときの予測課題

[fan-out実験](completion-fanout-result.ja.md)を使う教材案。利用者による理解・変更の評価は未実施。
通知を何箇所へ送るかより先に、結果・受信byte・宛先ごとの進行状態をどこへ置くか考える。

1. 同じI/O tokenを4宛先へ送る。各handlerがtakeする場合、成功回数を予測する。「4 handlerが呼ばれる」と「4箇所が結果を受け取る」を区別する。
2. 一度だけtakeして20 Bのmetadataをコピーする。source bufferを上書きした後でも使える情報と、失われた情報を列挙する。
3. 4宛先のうち1宛先のqueueだけを満杯にする。既に受理された他の宛先を再送しないために必要な状態を描く。
4. queuedな宛先を解除する。enqueue成功を配達完了と数えた場合と、handlerの受取を記録した場合で、解除後の状態を比較する。
5. その宛先が戻らない間に、次のI/Oが終端する。保持されるpublication・source record・bufferを指し示す。
6. captureの前と後でsourceをendする。どの通知を止められるか、結果コピーを取り消すために何が追加で必要か説明する。

予測した後に、次の検査と結果表で照合する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_fanout.py -q
```

`hint`は競合回収、`full`は宛先ごとの満杯、`remove`は再登録、`stalled`は停止recipient、
`end-before-copy`／`end-after-copy`はsourceとの寿命の違いを扱う。
配信を進める時間の上限はなく、明示的なturn／pump／rebindをどこで呼ぶかも利用者が決める。

変更課題では、画面表示をrequiredから外す案と、PD制御結果の記録だけをrequiredにする案を比較する。
「外す」の意味を、送らない・一度だけtryする・最新値に置換する、のどれにするか先に定義する。
今回のPublicationはrequired集合への配信なので、best-effortを同じmaskへ入れたまま無視する実装にはしない。
[用途側optionalの比較](completion-optional-result.ja.md)には、共通Publicationを変えずに1回try／最新状態を組んだhost結果がある。
表示が止まる間のdropと鮮度を予測してから照合する。targetの費用比較は未実施。
配達しない宛先が残ったときのrelease条件と、古いqueue payloadをどう扱うかも変更仕様へ書く。

最後に1→4宛先、容量1→2のRAM増分を結果表から計算する。
publicationにあるコピーだけでなく、宛先ごとのqueue内コピー・登録token・一時snapshotも数える。
source tokenだけの起動、単一ownerが結果を回収して用途側の状態を更新する方式、metadata配信のうち、
自分の用途に必要な意味と、削れる保持場所を説明する。採用は用途全体の測定で判断する。
