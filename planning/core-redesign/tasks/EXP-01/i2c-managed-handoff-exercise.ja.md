# I2Cの周辺返却と結果回収を分けて予測する

2026-10-21。[接続実験の結果](i2c-managed-handoff-result.ja.md)に対応する教材案。
公開APIの使い方を確定するものではなく、利用者が保持期間を理解できるかはレビュー前。

## 課題

I2Cを使うAとBは別のManagerで、同じDomainと周辺を使う。
Aの要求は受理時に外側bufferを借り、通信後のreapで結果を保持する。
Aが結果をtakeする時点と、周辺をcloseしてBへ渡す時点は異なってよい。
次の各段階で「clockを変更できるか」「Bがopenできるか」「Aのbufferを編集できるか」を予測する。

1. Aをopenしたが、まだsubmitしていない。
2. Aがsubmitしたが、dispatchしていない。
3. Aの通信が終端と判定されたが、reapしていない。
4. Aがreapしたが、closeしていない。
5. Aがcloseし、結果をtakeせず保持している。
6. Bが別PCLK宣言でopenして通信中に、Aが旧結果をtakeする。

## 再現と変更

repository rootで、ボード接続なしで実行する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_managed_handoff.py -q
```

該当traceは `test_backend_handoff_reconfigure_and_old_take_while_new_owner_runs`。
`EXP01_I2C_MANAGED_HANDOFF_EVIDENCE` に未作成directoryを指定すると、入力・出力・実行物を保存できる。
CLIの `select 0`／`select 1` は呼出し先Managerを選ぶ。
`backend rate` が周辺open、`close` が周辺close、`open client`／`end client` は利用者受付の操作である。
`change`／`publish Hz` はDomainの宣言を変えるだけで、clock registerを書き換えない。
出力のresourceは `[frequency, epoch, lease数, mask, 変更中]`、heldはA／Bの保持状態。
`bytes` の-1は借用中で読み取れないことを示す。

予測してから次の変更を個別に行い、状態・MMIO・byteが変わる位置を記録する。

- Aのcloseをreap前へ移す。拒否された後、誰が何を呼べば進むか。
- Aの結果が未回収の間、Bへ同じbufferをsubmitする。新しいbufferなら何が違うか。
- Aのreap後にcloseを省き、利用者takeだけ呼ぶ。clock変更は通るか。
- Bが通信中にAのcloseを重ねて呼ぶ。Bのlease数と通信結果が変化しないか。
- clock-onlyの外部leaseを追加する。I2Cをcloseした後にも変更が拒否される理由を説明する。
- AのQueued要求をcancel／client endに置き換える。取消結果のtakeと周辺closeの順を入れ替える。

## 評価記録

予測表、実際のtrace、必要だった追加状態、誤解した操作名、原因を記録する。
「処理が進まない」が未dispatch・未step・未reap・未close・結果未回収のどれによるか、呼出し順と保持objectから説明できるかを確認する。
個別tokenは呼出し先Manager内だけで解釈するため、同じ数値を別Managerへ渡して所有者を識別する使い方はしない。
時間制約・実IRQ安全性・実pin競合をこのhost課題の正解へ混ぜない。
人による予測・変更・説明を取得して初めて教材評価とし、テストの成功だけで完了にしない。
