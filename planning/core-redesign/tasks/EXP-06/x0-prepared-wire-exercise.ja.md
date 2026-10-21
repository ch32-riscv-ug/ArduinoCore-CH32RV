# preparedと今のreadyを分けて予測する

2026-10-21。[X035準備の用途接続](x0-prepared-wire-result.ja.md)に対応する教材案。人の評価は未実施。

## 予測する

I2C準備は一度成功した。しかし別の処理がclock・gate・route・pinを変える可能性がある。
次を実行前に予測する。

1. preparedがtrueのままrouteを変える。次のopenは通るか。
2. 準備書込みを一つ無視させる。失敗時のleaseは誰が持つか。
3. FaultHeldのまま用途stepを繰り返す。新しいwire取引は受理されるか。
4. I2C結果は未takeだがbackendはreap済み。このとき再prepareやclock宣言変更はできるか。
5. RCC模型を変えずDomainの周波数だけpublishする。どこで拒否されるか。
6. peerが新clock宣言で通信中に、旧結果をtakeする。pinやclockを再設定するべきか。

## 再現する

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_prepared_wire.py -q
```

`test_open_rechecks_board_then_recovers` では、拒否しても状態確認の読出しは必要なことを確認する。
`test_partial_prepare_failure_holds_lease_until_abandon` では、abandonが実際の停止処理ではないことを説明する。
`test_old_result_survives_reprepare_and_peer_at_new_clock` で、周辺leaseと結果bufferの寿命を別々に追う。

debug解放宣言を省く、packageをF7 route 0へ変える、RCC模型の変更を省く、の各条件を個別に試す。
動作しないrouteの小さいimageを、動作する用途の優れた結果と比較しない。
公称PCLKと設定値の一致、実際の周波数、物理pad、実wireの確認を区別する。

予測、変更file・差分、追加状態、実trace、誤解と支援回数、実施順を記録する。
モデルの生成速度・自己採点を、人の理解しやすさの評価には使わない。
