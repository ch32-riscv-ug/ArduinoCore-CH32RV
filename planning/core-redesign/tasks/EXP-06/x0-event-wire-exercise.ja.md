# 通知が届くまで結果を保持する

2026-10-21。[X0完了通知の用途実験](x0-event-wire-result.ja.md)に対応する教材案。人の評価は未実施。

## 予測する

表示commandは終了し周辺を返したが、用途はまだtakeしていない。
FIFO容量は1で、無効hintが既に入っている。
次の手順で何が進み、どのbufferが使えるかを予測する。

1. pumpする。満杯でも元の結果は残るか。
2. そのまま用途stepを繰り返し、PPSの入力と仮想PD完了を与える。
3. queueの無効hintをdeliverする。
4. pumpし直した後、deliver前に宛先を解除する。
5. 再bind→pump→deliverする。いつbufferが返るか。
6. 用途take後、新requestの終端へ旧hintが届く。

「I2Cが終わった」「周辺を返した」「hintが配達された」「結果をtakeした」を別に説明する。
通知が届いても、handler自身はbufferを返さない今回の候補を対象にする。

## 再現する

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_event_wire.py -q
```

`test_queue_full_unbind_and_retry_keep_the_result`、
`test_duplicate_old_hint_cannot_collect_the_next_request` を読む前に、上の予測を記録する。
`test_notification_stall_does_not_prevent_pps_control` では、
PPSと表示・計測・仮想touchが同じように進むとは限らないことを確認する。
仮想PDの成功を、実PDの期限を守った結果とは解釈しない。

## 変更と記録

- pumpだけ／deliverだけ／用途stepだけを止め、どの状態で保持が続くか比べる。
- hint配達後に再pumpし、次requestへ古いhintを渡す。
- 別ownerが周辺を取得した状態でdeliver／takeし、MMIOが発生しないことを確認する。
- 内部callbackをuser扱い・未参加に変え、loop外の配達と明示配達を比べる。

予測、変更file・差分、追加状態、実trace、誤解・支援回数・実施順を記録する。
便利さと保持の波及、RAM／Flash費用を分けて説明できるか確認する。
モデルの自己採点やこのworksheetの作成を、人の学習効果の評価にはしない。
