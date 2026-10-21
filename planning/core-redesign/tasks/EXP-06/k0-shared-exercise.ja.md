# 差分takeとsnapshot公開に割込みが来たら

2026-10-21。[K0共有実験](k0-shared-result.ja.md)の教材案。利用者評価は未実施。

予測してから、各操作を個別に試す。

1. 差分1をtakeして0に戻す途中でIRQが+2する。いつ+2を処理するか。
2. 公開positionが255→256になる途中にread開始が来る。どの8 Bを返すか。
3. 通常側がqueueをpopする途中で、新しい受信frameがSTOPで完成する。
4. ready確認の直後に入力が来る。その呼出しで進まなくても、何を残す必要があるか。
5. 元からIRQ禁止の状態で、内側のscopeが終わる。無条件にenableしてよいか。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_k0_shared.py -q
```

`test_lost_delta_negative_control_and_deferred_arrival` と
`test_snapshot_publication_is_atomic_under_declared_irq_gate` の保護有無を比較する。
禁止区間をcallback／実行器まで広げず、共有copy・takeと通常計算の間へ分けた理由を説明する。

C-bridgeはframe用event FIFOではなく共有FIFO＋集約通知であることにも注意する。
便利さ・追加metadata・未使用payload領域の費用を分け、同じ意味の方式だけを比較する。
予測、変更file・差分、実trace、追加状態、誤解・支援回数・実施順を記録する。
この模型やCSR命令の存在を、実IRQ時間・I2C ACK・物理overrunの確認には読み替えない。
