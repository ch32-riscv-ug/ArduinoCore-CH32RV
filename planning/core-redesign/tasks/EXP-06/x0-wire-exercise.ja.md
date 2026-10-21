# X0の取引分割と受信値を予測する

2026-10-21。[N01の用途接続](x0-wire-result.ja.md)に対応する教材案。利用者評価は未実施。
公開API、実clock／IRQ、loop外／delayの採用方針を決めるものではない。

## 予測

表示のcommandとdata、計測のbus wordとcurrent wordを、それぞれ別の取引にした。
取引の間には別ownerが動ける。開始前に次を予測してからtraceを確認する。

1. 表示commandが成功しdataが失敗したとき、frame数・表示revision・再試行はどうなるか。
2. bus wordだけ新しい値を得て、current wordが失敗したとき、画面の計測値とstaleはどうなるか。
3. sourceをsubmit後・completion後・take前に変えたとき、どの値を公開するか。
4. 2回のreadが成功した場合、同じ時点のADC結果・新しい変換だと判断できるか。
5. 完了済みでもtakeせず停止した場合、別ownerはいつ進めるか。
6. 仮想touchの処理成功から、実touch controllerの動作について何が分かるか。

## 再現

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_wire.py -q
```

`test_two_register_reads_are_not_a_coherent_or_submit_time_sample` は、
bus wordとcurrent wordを異なる時点で取得する例。
`raw` は二つのraw registerを与える模型入力で、実際のcalibrationや電圧印加ではない。
`time` はfake完了とPD模型を進め、`step` は通常側の回収・用途処理を進める。
tickを実msへ換算しない。

変更前に結果を予測し、個別に次を試す。

- current wordだけを失敗させ、直前に得たbus wordを公開しない理由を説明する。
- commandとdataの間で通常処理を止め、controllerとアプリの「表示済み」が同じ意味か確認する。
- 元のTX bufferを送信開始後に書き換える。コピー後に元bufferを使える境界と、bus内部packetの保持期間を区別する。
- 再接続前のPD完了を渡し、取引完了と新sessionの確定値が混ざらないことを確認する。
- 同じ入力をR/A/B/Cへ渡し、packetが変わる変更と、実行器だけが変わる変更を区別する。

## 評価記録

事前予測、変更file、差分、追加状態、実trace、誤解した名前、支援回数と実施順を記録する。
「transport成功」「新しいADC変換」「値の正確さ」「同時取得」を一つのvalidに混ぜていないかを確認する。
このworksheetの作成と機械検査の成功を、人の理解しやすさの結果にはしない。
