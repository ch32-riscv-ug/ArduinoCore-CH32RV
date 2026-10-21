# 周辺を返してから旧結果を回収する

2026-10-21。[N02直接接続](x0-register-wire-result.ja.md)の教材案。人による評価は未実施。

## 予測する

表示commandを受理し、Masterが停止してbackendを回収した。
用途の結果はまだtakeしていない。その間に別ownerが同じI2C周辺を使う。
次の操作が何に触れるか、実行前に予測する。

1. timeだけを進める。Masterの状態も進むか。
2. 取消要求を出す。bufferとleaseを直ちに返せるか。
3. 停止後のreap／closeを済ませる。用途のRXを上書きできるか。
4. 別ownerが取得・通信開始した後、旧用途のtakeを行う。
5. 旧用途が続きのdataを送ろうとする。別ownerの通信を壊さずに何を保持するか。
6. pin準備の宣言がfalseのとき、受理・送信・leaseはどうなるか。

## 再現する

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_register_wire.py -q
```

`test_old_application_take_while_peer_owns_peripheral` で旧take時のMMIOが空であることを確認する。
`test_error_prefix_resources_and_recovery` では、エラー時に何byteをissued／confirmedとして保持したか確認する。
表示の一部を送れたことと、frameを正常に表示し終えたことを区別する。

nativeの `wire` はhostで刺激と明示pollを展開する補助。
targetに同じblocking関数が存在すると解釈しない。
`poll()`一回と取引全体を終えるための複数回を分けて数える。

## 変更と記録

- 通常進行を止める位置を、受理前／dispatch後／終端後／take後へ移す。
- TX途中のfailureを入れ、受信先や前の計測値が成功扱いで更新されないことを確認する。
- pin宣言を拒否させてから戻し、誰が何を再試行するか説明する。
- 公称PCLKの宣言変更と、実RCC・pin・clock精度の検証の違いを説明する。

事前予測、変更file・差分、必要な状態、実trace、誤解、支援と実施順を記録する。
このworksheetやhost検査の成功から、実機での安全な停止・IRQ共有や学習効果を確定しない。
