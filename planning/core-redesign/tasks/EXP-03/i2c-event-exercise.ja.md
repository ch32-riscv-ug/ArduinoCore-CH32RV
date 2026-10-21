# I2C完了通知の配達と発生元を予測する

2026-10-21。[資源管理付きI2Cとcallbackの実験](i2c-event-result.ja.md)に対応する教材案。
公開APIや自動進行方針の決定ではなく、人による予測・変更・説明の評価は未実施。

## 予測する

AがI2Cを完了し、reapで結果を保持してから周辺をcloseした。
Aの完了hintはFIFOへ入ったが、callbackはまだ呼ばれていない。
その間にBが同じ周辺をopenして通信を始めたとする。

- Aのbufferは誰が保持しているか。Bは同じbufferを借りられるか。
- Aのcallbackを実行するためにI2Cのleaseを取り直す必要があるか。
- Aのcallbackがtakeしたとき、Bの通信やleaseへ何が起こるか。
- Aのcallbackを呼ばずにさらにsubmitすると、何が受理容量を消費するか。
- 宛先の解除でhintが消えた場合、何を根拠にretryできるか。
- enqueue時にackした場合と、callbackのtakeまでpendingを残す場合で何が違うか。

## 再現して変更する

repository rootで実行する。ボード操作はない。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_event.py -q
```

`EXP03_I2C_EVENT_EVIDENCE` に未作成directoryを指定すると、native実行物・入力・出力を保存できる。
`test_old_source_callback_during_another_backend_owner_never_touches_its_lease` がA／Bのtrace。
`select` は通常操作の呼出し先を選ぶが、登録済みcallbackのcontextは変更しない。
`bind domain opted` は選択中Managerへ固定したcallbackを登録する。
`pump generation index client` はそのManagerの登録先へhintを送り、`turn limit opportunity profile context` で配達機会を与える。
各宛先のFIFO容量は1で、二つの宛先が一つの容量1 FIFOを共用するわけではない。

予測してから、個別に次の変更を試す。

1. Aのturnを先送りし、その間に別clientの要求を完了させる。Full後に何を保持し、どの順でretryできるか。
2. pump後、callback前にclient end／明示ack／直接takeを一つずつ追加する。callbackがskipする場合、誰がbufferを返すか。
3. pump後に宛先をunbindして再bindする。新しい宛先generationと元のrequest generationを区別する。
4. Aのhintが残る状態でAのrecordを直接回収・再利用する。旧hintが新結果を回収しないことを確認する。
5. AfterLoop／Delay相当のturnについてM/S/UとSystem／User／opted-in Userを変える。実コアのhookではなく、実験上の明示機会であることを確認する。

## 発生元の反例

`test_counterexample_request_token_alone_does_not_identify_source_manager` は、意図的にAのtokenをBの登録先へ送る。
CLIの `pump_to destination generation index client` は、この誤接続を再現する実験用操作。
A／B両方が `(1,0,0)` の要求を保持していれば、Bに固定したcallbackはB自身の結果を正当なものとして回収する。
回収元が意図と違うことを、callback_logのsource、bufferのbyte、各Managerのnoticeから説明する。

「番号が一致した」「callbackが成功した」だけで発生元を判断しない。
発生元をmessageへ含める方法、発生元と宛先を固定する方法などについて、追加状態・呼出し制約・誤接続時の挙動を書き出す。
この課題だけで最終方式を選ばない。

## 評価記録

予測、実trace、変更差分、誤解した操作名、必要だった追加状態を残す。
I2Cの停止・backend回収・周辺返却・結果回収・hint配達を別々に説明できるかを確認する。
callbackの実行回数だけで、正しいsourceの回収が成立したと判定しない。
人による評価を取得して初めて教材レビューとし、host成功だけで完了にしない。
