# EXP-03準備：宛先mailboxとI/O・timerの結合

2026-10-20。入力HEAD `1e3b240`。
有限workと同じ一巡・実行機会の条件で、宛先FIFOと集約通知を検査した。
通常直列文脈の試作で、ISR投稿、実周辺、R/A/B/Cの完成用途比較や公開配信方式の採用は未完了。

## FIFOと集約通知を分ける

`EventExecutor<Message,N,M>` はN宛先、それぞれM件のpayload FIFO、32 bitの集約通知を持つ。
今回検証したpayloadはsequence／反転値／origin／checkの16 byte固定型。
sendはAccepted／Full／Staleを返し、fullで拒否した入力を受理した扱いにはしない。
notifyは有効宛先への非zero maskをORし、同じbitの複数到着を数えない。
押下の各回やPPSの個々の指示等、全件を保持する必要のある情報をbitで代用しない。

一巡は対象ごとに最大1回。payloadがあれば1件、集約bitがあればそのsnapshotを一つのDeliveryに載せる。
一巡開始時にpayloadを凍結するが、mailboxのslotは対象のhandlerを呼ぶ直前まで占有する。
先行handlerが、まだ呼んでいない対象の満杯mailboxへ追加の入力を押し込めるようにはしない。
選択時に消費した旧bitと、その後の新bitを分け、実行中の再投稿は次巡に残す。
新しいpayloadも、初めからsnapshotに存在したものと取り違えない。

巡回位置、有限件数、M/S/U、初期化前・setup・ISR・callbackの拒否条件は
[workの準備](work-preparation.ja.md)と同じ。
解除は未配達のFIFOとbitを破棄し、世代を変えて同じindexを再利用しても古いsnapshotを呼ばない。
処理中のhandlerそのものを中断する意味ではなく、contextと実行器は現在の呼び出しが戻るまで保持する。
静的容量は解除しても減らない。世代上限のslotは退役する。

fan-outは送信側が1／2／4宛先へのtry操作を明示的に繰り返す条件とした。
宛先ごとの受理結果を保持し、途中でfullになっても先の受理を取り消したり、後の宛先を黙って省略したりしない。
全宛先への原子的な一括受理APIを採用した意味にはしない。

## I/Oとtimerの結合

I/Oの終端結果は[backend slot](../EXP-01/io-preparation.ja.md)にsticky保持する。
mailboxが満杯でも独立した集約bitで起動を受理でき、終端はhandlerがtakeするまで残る。
bit受理後に通知をackしても、結果とbufferの所有は回収まで維持する。
配達前にend／宛先解除した場合はcallbackを抑止し、backendの明示cleanupでbufferを返す。
同じindexへ再登録した新しい宛先を、古いsnapshotから呼ばない。

[timer](timer-preparation.ja.md)も期限情報を別に保持し、bitは条件を確認する起動だけに使う。
古いbitが残っていても、再armで置換した古い期限をuserの期限処理として通知しない。
内部の宛先handlerが条件を確認する呼び出しと、有効な期限を回収する処理を検査で分けた。
payloadに古いtimer世代を載せる案、複数timer管理、wait接続は別の検査として残る。
[完了枠予約とbus割当て](completion-arbiter-result.ja.md)は別のfake-I/O単位で比較した。
追加の[配信結合](completion-event-result.ja.md)では既存headerを変えずnative74件を検査し、満杯retry・宛先解除・end・古いtokenを接続した。
queue格納時ackで元のpendingを消すと、解除後に再通知が失われる反例を確認した。
上記のbit受理時ackも、宛先解除後の再通知まで保証する契約ではない。結果保持と再通知可能性は別に扱う。

## Hostの証拠

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_event_executor.py -o junit_family=xunit1
```

ASan／UBSan付き61件成功。既存plugin由来のexperimental warning61件。

- N=1／4／8、一巡上限=1／4／8の9条件で自己・相互投稿、有限性、公平性、再帰拒否を確認。
- 先行handlerからのsendが、まだ配達していない満杯の対象に対してFullとなることを確認。
  追加bitは旧snapshotの配達で消えず、次巡へ残る。
- M/S/U、未参加user、初期化前、setup、ISR、callback、解除と8 bit世代退役を検査。
- fan-outの1／2／4宛先で、途中のfullを含む各受理と実際の配達を照合。
- 満杯mailbox、I/O終端、timer期限を同時に保持し、Mで配達せずSで回収する結合を確認。
- 同じ一巡の先行handlerでI/Oのendと宛先再登録を行い、旧callbackを抑止。
  bufferをcleanupした後の新しい要求だけを、新しい世代へ配達する。
- N=1／4／8×M=1／4／8×5 seed×10,000操作をPythonの独立deque・bit・巡回台帳へ照合。
  生成後に再投稿を止めてdrainし、最後に残った受理payloadも全件確認する。

## ABI費用の参考値

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix event \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-event-link
```

19ケース×2 ABI×LTO有無、76構成がlink成功。
未使用のevent／bridge各4条件、計8行で追加Flash／静的RAM／init_arrayが0 byte、関連symbolなし。
RV32E・LTOなしの例。単位byte。

| 入口 | Flash image | 静的RAM | Flash増分 | RAM増分 |
|---|---:|---:|---:|---:|
| baseline | 30 | 24 | 0 | 0 |
| 1宛先・FIFO1・上限4 | 1258 | 84 | 1228 | 60 |
| 4宛先・FIFO1・上限4 | 1598 | 240 | 1568 | 216 |
| 4宛先・FIFO4・上限4 | 1626 | 432 | 1596 | 408 |
| 8宛先・FIFO8・上限4 | 1662 | 1344 | 1632 | 1320 |
| I/O・timer結合、1宛先・FIFO1 | 2032 | 180 | 2002 | 156 |
| I/O・timer結合、4宛先・FIFO4 | 2444 | 528 | 2414 | 504 |

入口の明示登録・token配列・自己／相互投稿・payload全wordとbitの観測を含む。
結合modelは32 byteの保護buffer、slot、timer、転送FNVを含むため、診断なしの製品費用とはしない。
FIFOのないwork単体と同等機能ではなく、この表だけでB／Cの優劣を決めない。
完成用途で同じpayload保持・失敗処理を揃えた比較が残る。

snapshotは通常stackを使う。RV32E・LTOなしの `.su` のdata_entry frameは
4宛先・FIFO4で172 byte static、8宛先・FIFO8で300 byte static。
handler／send等の呼び出しとIRQの重なりを含むstack peakではない。
対象数と容量を大きくする費用を、完成用途の静的RAMとstack両方へ計上する必要がある。

初回のbaselineはevent単体で不要なclockまで観測していた。
baselineのclock参照を取り除き、clockを使う結合modelだけにその費用を残して全76構成を再実行した。
最終表へ初回行を混ぜず、初回の入力と生成物も保存した。
全行は [event-size-matrix.csv](event-size-matrix.csv)、入力・tool・生成物hashは
[event-measurements.json](event-measurements.json)。

host `work/exp03-event-host-20261020/`、最終link `work/exp03-event-link-20261020-final/` に生記録と入力コピーを保存した。
移送archive `work/exp03-event-20261020-evidence.tar.gz`、3,783,718 byte、
SHA-256 `33f5a17694e2731aafd88464369a33b4ac1c4186cbe440b18c6685271efb1a8f`。
command・終了値・stdout／stderr・map・symbol・disasm・section・.su・hashを照合した。

## 継続

次はA/B/Cで同じI/O・timer・データ保持を揃えた進行・待機接続と、K0／X0の結合へ進める。
通常／IRQ／別コアproducerとwrapperの進行条件、ISR投稿の保護、完了枠予約／arbiter、
独立stack、実cycle・stack・protocol、未実施のEXP-01／04／05と教材評価は残る。
共通イベントをsystemだけにするかuserへ公開するかは、この成功だけで確定しない。
