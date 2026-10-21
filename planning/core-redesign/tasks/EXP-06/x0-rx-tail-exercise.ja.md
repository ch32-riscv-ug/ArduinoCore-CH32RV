# 受信したbyteと公開できる結果を分ける

2026-10-21。[受信末尾実験](x0-rx-tail-result.ja.md)の教材案。人による評価は未実施。

## 予測する

前回5000 mV／1000 mAを表示できた。
次の計測中に新しいbyteを受け取ったが、まだ停止と結果回収が終わっていない。
次の違いを、実行前に予測する。

1. FIFOに1 byteある状態で故障する。排出したbyteは受信成功数へ入るか。
2. 2 byteを読んだ後、停止前に故障する。表示やpayload8 Bは更新するか。
3. 故障はないがStopで待っている。時刻を進めればbufferを再利用できるか。
4. read後、take前にraw sourceを変える。どちらの値が公開されるか。
5. 終端を確定・reapした後にerror flagを与える。旧結果を書き換えてよいか。
6. 失敗結果を知らせるqueueが満杯で、さらに宛先を解除する。結果はどこに残るか。

## 再現と記録

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_rx_tail.py -q
```

`test_failure_before_or_after_rx_and_immutable_terminal` で、Dataのread数とCompletion.receivedを比べる。
`test_successful_rx_does_not_release_until_quiet_and_uses_received_bytes` では、
lease、loan、terminalが別々の時点で変わることを確認する。
FIFO1の条件から、実RX2処理のread命令間でのIRQ挙動を結論しない。

予測、変更file・差分、必要な状態、実trace、誤解と支援回数、実施順を記録する。
「byteがある」「有効な受信数」「取引成功」「新しいADC変換」「正しい物理量」を区別して説明する。
hostのclockやquiet刺激を、実機での電気的な停止・復帰の観測として扱わない。
