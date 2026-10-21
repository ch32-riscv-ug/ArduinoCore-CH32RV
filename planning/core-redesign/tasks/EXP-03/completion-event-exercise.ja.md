# 通知の到着とbufferの返却を分けて考える

対象は[完了枠とイベント配信](completion-event-result.ja.md)の実験harness。
教材案であり、利用者が理解・変更できるかの評価はまだ行っていない。
時間保証や割り込み知識より先に、小容量のrecordとqueueが何を保持しているかを追う。

## 予測してから実行する

1. client0の読出しをfinishし、tokenを容量1のqueueにsendする。まだturnは呼ばない。bus・record・bufferのうち、次の利用者へ渡せるものを予測する。
2. client1もfinishする。そのtokenのsendがFullになったとき、retryに必要な状態がどこに残っているか図にする。
3. client0へのイベントをenqueueした後、宛先を解除する。enqueue時に元のnoticeをackした場合と、takeまで保持した場合を分け、再登録後の進行を予測する。
4. enqueue後にclient0をendする。内部handlerの起動、結果を利用者へ渡す処理、bufferの返却が、それぞれ起こるかを予測する。
5. old要求を直接takeして次の要求を受理し、old tokenを配達する。client番号だけで回収すると何が壊れるか説明する。

検査は次で動かせる。物理boardは不要。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_event.py -q
```

`test_lifetime_and_backpressure`が満杯・end・古い要求・再入、
`test_registration_loss_and_ack_timing`が宛先解除とackの時点、
`test_delivery_permissions`がM/S/Uと実行機会の対照を扱う。
自分の予測と違ったところを、要求・宛先の二種類の世代と、pending・queue・resultの別々の状態で説明する。

## 変更する課題

実験用driverを別copyで変更し、client0のhandler内からclient0の次要求をsubmitする。
takeの前後で受理結果がどう違うか、別clientがqueuedならdispatch順がどうなるかを記録する。
元のhandlerへ新しい処理を入れる際は、再帰turnを呼ばず、次の明示的な進行機会へ返す。

次にUser opt-inを外し、Delayでturnを呼ぶ場合とExplicitの場合を比べる。
処理されないことと結果が消えることを区別し、どこで回収するかを呼出し側に書く。

最後にqueue容量だけを増やす案を検討する。
宛先解除による破棄、古いtoken、end後のcleanupが容量増加で解決するかを説明する。
[直接回収との費用比較](completion-delivery-result.ja.md)には、固定1宛先・容量1の測定値がある。
そのRAM52 B増分の内訳を説明し、自分の用途に直接回収だけで足りるかを考える。
容量増加によるFlash／RAMの変化は未測定なので、hostの成功だけからMCUへの採用は決めない。
