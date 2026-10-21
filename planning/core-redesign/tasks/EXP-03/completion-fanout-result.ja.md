# 完了通知のfan-outと結果コピーのfan-out

2026-10-21。実装commit `4479b05`。[入力](../../completion-fanout-inputs.json)、
[全108 ELF](completion-fanout-size-matrix.csv)、[host・hash・差分](completion-fanout-measurements.json)を保存した。
[1宛先の結合](completion-delivery-result.ja.md)から、1／2／4宛先・各queue容量1／2へ比較を広げた。
製品コア・USB・既存のArbiter／IoSlot／EventExecutor／配信接続headerは変更していない。
共通配信を採用する判断はP5へ渡す。

## 二種類のfan-out

同じsource tokenを複数宛先へenqueueすると、全handlerを起動できても、
最初のhandlerのtakeで元の結果が回収され、残りは同じ結果を回収できない。
1／2／4宛先の対照では回収成功は常に1回、残り0／1／3回は古い通知として棄却された。
これは複数consumerへ結果を渡す仕組みではなく、複数箇所を起こす手がかり。

別の試作では、独立したpublication枠を1個予約してからsource結果を1回だけtakeし、
要求tokenとCompletionをコピーしてpublicationへ保存する。
各recipientのqueueには、publication世代・recipient番号・同じ値のコピーを送る。
1 recipientのqueueが満杯でも、他のrecipientの受理を巻き戻さず、未受理部分だけを明示pumpでretryする。
publicationがBusyならsourceをtakeせず、元のbufferと結果を保持する。

コピーするのはmetadataだけ。今回のRV32ではValueは要求token8 B＋Completion12 B＝20 B、
queue payloadはpublication世代とrecipientを加えて28 Bとなる。
受信byte列はコピーも共有もしていない。source bufferはcapture時のtakeで返却し、上書き可能になる。
旧digest等のmetadataが届くことと、旧受信byteへアクセスできることを区別する。

## 保持と受取の契約

| 段階 | source record・buffer | publication | recipient queue |
|---|---|---|---|
| reserve失敗 | 変更しない | 他の配信中等 | 変更しない |
| reserve成功・capture前 | source側に保持 | 予約済み | まだ送らない |
| source take・publish成功 | 返却済み | metadataコピーを保持 | 宛先ごとに送信可能 |
| enqueue成功 | 返却済み | 当該recipientをqueuedと記録 | コピーを保持 |
| receive成功 | 返却済み | deliveredと記録 | handler開始時にpop済み |
| 全required recipient受取後のrelease | 返却済み | 枠を返却 | 対象payloadは消費済み |

receiveは、現在のpublication世代・required recipient・queued状態・宛先の登録世代を再検証し、
handlerのlocal Valueへコピーして受取を記録する。利用者の業務処理完了や永続化の確認ではない。
同じpublication内では、受取済みrecipientへpumpしても再enqueueしない。
全required受取後でも、明示releaseまで次のreserveを拒否する。

queuedな宛先を解除すると、EventExecutorはそのpayloadを破棄するが、publicationは自動解放しない。
callerが旧登録の無効化を確認したうえでreplacementを選び、明示rebindすると、そのrecipientだけ再送できる。
旧登録がまだvalidな場合のrebindは拒否し、別queueへ勝手に二重配信しない。
再登録先へ渡してよいかはcallerの判断であり、利用者権限の自動引継ぎは提供しない。

停止したrecipientを128回の明示進行機会で復帰させない例では、publicationは未完了のまま残った。
次のsource結果も回収できず、そちらのbuffer借用も続く。
その後の明示再登録・rebind・retryで両方を回収できたが、時間上限や停止recipientの強制切離しはない。
「全員をrequiredにする」ことで、遅い一者をどの資源の保持まで波及させるかが変わる。

sourceのendも境界が変わる。
capture前のendはnoticeを抑止するためpublicationを取り消してsourceを明示cleanupする。
capture後のendは、既に別所有となったコピー配信を取り消さない。
返却済みbufferを書き換えても、recipientには旧metadataが届くことを確認した。
コピー後もendで一括取消したい場合は、別の配信寿命・revocation契約が必要になる。

## Hostで確認したこと

ASan／UBSan、`-Wall -Wextra -Werror`でnative356件＋workspace3件、計359件が通過した。
専用3 record／共有2 recordの両source、1／2／4宛先、queue容量1／2を組み合わせた。

- 満杯となるrecipient集合と解除する集合を全列挙：計176シナリオ。先の受理を保ち、受取は各recipientにつき1回。
- required集合の全非空部分集合：76シナリオ。集合外へenqueueしない。
- source tokenの競合回収、枠の未解放、古いpublication、予約失敗・取消と出力保持、8 bit世代退役、end前後、停止recipient：96シナリオ。
- handler内で同じturnの後続宛先を解除・再登録：8シナリオ。古いsnapshotは呼ばず、新しいqueueは次のturnで進める。再帰turnも拒否。

古いpublication payload、範囲外recipient、異なるtarget世代、messageなし通知の受取を拒否する。
8 bitの255回reserve／abandon後はExhaustedとし、世代を周回させない。
検査の最後に全source bufferを返却し、publicationとqueueを明示的に空にする。
fixtureはtest module内で、conftest.pyは追加していない。

最初の344 nativeシナリオに、停止recipientと追加の拒否条件を加えて最終356件とした。
初回binaryで元の344入力を再実行し、最終binaryのstdoutと全件一致した。
独立した全面状態機械との差分検査ではなく、C++の境界assertとPythonの期待回数・集合数を照合した証拠である。

## RAMとFlash

RV32のLTO off／onとも、共通source本体は専用116 B／共有80 B、保護付きbuffer3個108 B、共通harness117 B。
以下のRAM増分は、同じsourceの直接回収probeに対する差分。専用／共有で増分は同じ。
publicationはmetadataと保存した宛先、routeは次の予約／rebindにcallerが使う登録token列。
この試作では二箇所にrouteを保存しているので、最小実装の下限値ではない。

| 宛先 | queue容量 | publication B | EventExecutor B | caller route＋ticket B | RAM増分 B | 全RAM 専用／共有 B |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 36 | 64 | 12 | 112 | 456／420 |
| 1 | 2 | 36 | 92 | 12 | 140 | 484／448 |
| 2 | 1 | 44 | 120 | 20 | 184 | 528／492 |
| 2 | 2 | 44 | 176 | 20 | 240 | 584／548 |
| 4 | 1 | 60 | 232 | 36 | 328 | 672／636 |
| 4 | 2 | 60 | 344 | 36 | 440 | 784／748 |

LTOありのimage全体。各欄は専用／共有の順で、共通harness・追加操作経路・supportも含む。

| 宛先 | queue容量 | V003 image B | X035 image B |
|---:|---:|---:|---:|
| 1 | 1 | 3,624／3,624 | 3,304／3,312 |
| 1 | 2 | 3,672／3,672 | 3,320／3,336 |
| 2 | 1 | 3,936／3,928 | 3,592／3,600 |
| 2 | 2 | 4,032／4,024 | 3,632／3,624 |
| 4 | 1 | 3,976／3,960 | 3,592／3,624 |
| 4 | 2 | 4,032／4,024 | 3,624／3,648 |

直接回収の対照は、前回の8構成とimage hash・RAMが一致した。
LTOありの直接imageはV003 2,008／2,016 B、X035 1,792／1,808 B。
新しい配信は複数consumerへの結果コピーという別の意味を持ち、追加のreserve／publish／rebind等も公開したprobeなので、差分をそのまま同等機能の割増率にはしない。
1宛先のtoken配信で測ったRAM52 Bとも、保持するデータと意味が違う。

最大単一frameはLTOありで、宛先1／2／4の順にV003 96／144／232 B、X035 144／176／272 B。
EventExecutorの有限snapshotが宛先数で増えることも費用に含まれる。
呼出し鎖・割り込み・アプリを合算したstack peakではない。
全108構成で.dataは0 B、allocator・constructor arrayの混入なし。
未使用48構成は追加RAM・objectが0 B、imageは同じABI・LTOの空8 B probeとhash一致した。

## 再現と未検証

```sh
env EXP03_COMPLETION_FANOUT_EVIDENCE=work/exp03-completion-fanout-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_fanout.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_completion_fanout.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-completion-fanout-abi-NEW
```

出力先は新しいdirectoryを使う。
local archiveのpath・byte数・SHA-256は[測定台帳](completion-fanout-measurements.json)のarchive欄に記録した。
入力snapshot、最終356 traceとbinary、変更前binaryと344再実行、108 ELF／binary／map／symbol／section／disassembly／frame、
全commandとcompiler／binutils／libgcc hash、直接回収の対照8 image、集計scriptを含む。
ABIはreset／CRT・vectorを持たない独立linkの構造測定で、起動・実I/O・実行時間の検証ではない。

コピー配信の採用、required／best-effortの使い分け、配信取消・宛先の放棄・利用者処理完了の確認、
buffer byte列を複数箇所で共有する所有規則、複数publication、ISR・実driver接続、用途全体・実機stackは残す。
このmetadata試作だけで、小型用途に汎用fan-outを常備すべきとは判断しない。
[教材案](completion-fanout-exercise.ja.md)は、人による評価前。
