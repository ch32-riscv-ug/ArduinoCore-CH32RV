# busを渡す時点とbufferを返す時点

[I2C handoff実験](i2c-handoff-result.ja.md)の教材案。利用者の予測・変更の評価は未実施。

1. client0の要求をsubmitしてまだdispatchしない。結果枠、外側buffer、backendがそれぞれ使えるかを予測する。
2. register driverがTerminalになったが、reapしていない。client1のdispatchとclient0のtakeは可能か。
3. reap後、client1をdispatchする。client0のbufferを読みたい場合、どの操作を待つ必要があるか。
4. client1を先にtakeする。二つのbufferの編集可否がどう変わるかを追う。
5. client0のnoticeだけackする。record・buffer・backendのどれが解放されるか。
6. Queued取消と、readのADDR後の取消を比べる。取消要求と停止・結果確定を区別する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_handoff.py -q
```

buffer内のbyte列、結果record、backendのViewを別の箱として描き、各段階のownerを記入する。
受信prefixはresultのreceived、送信の確認済みprefixはconfirmedで調べ、issuedを送信成功byte数と読み替えない。

変更課題では、reap後も画面更新側のtakeが遅れる用途を考える。
専用3 recordと共有2 recordで、別clientの受理がどこで止まるかを予測する。
queueを増やす前に、bufferを返せる利用者処理の境界と、コピーして保持すべきmetadataを明記する。
backendだけを閉じられる状態でも、GPIOやclockの台帳が自動で返却されるとは限らないことも説明する。
