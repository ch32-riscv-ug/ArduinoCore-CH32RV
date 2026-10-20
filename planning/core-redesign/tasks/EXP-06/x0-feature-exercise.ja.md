# X0変更課題：使わないタッチの費用を説明する

これは[機能除去の実験](x0-features-result.ja.md)を読むための教材素材。
説明を読んで利用者が予測・変更できるかの人による評価は未実施。
型名は試作用で、製品Arduino Coreの公開APIはまだ決めていない。

## 変更前に予測する

用途は、PPS設定・計測・表示・入力。タッチを足すと8 byteの通信を50回／秒追加する。
次の違いを、数値表を見る前に説明する。

1. `runtime0` はタッチを呼ばないのに、なぜRAMが残り得るか。
2. アプリ全体が未使用の場合のlink除去と、使っているアプリのタッチだけを外すことは何が違うか。
3. タッチを外すとbuffer以外にどんな状態・検査枠が不要になるか。
4. `on` は有効／無効を実行時に切り替える能力がない。この制約は用途に合うか。
5. 通常処理を止めたとき、仮想busの完了とbufferの返却は同じ時点か。

予測はFlash・静的RAM・stackを分ける。実際に取得していない処理時間は数値で埋めない。

## 一つの変更で何が増えるかを見る

`tests/unit/core_redesign/x0_features.hpp` の同じ候補で型を選ぶ。
Aの例は `FixedX0<2, false>` から `FixedX0<2, true>` への変更。
入力Queue、PD、画面の状態機械を複製せず、タッチの状態とbus枠が追加される条件を追う。
R・B・Cも同じ有無で比較し、Cの固定1宛先という能力差も読む。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_features.py -k same_baseline_and_fifty_hertz_workload \
  -o junit_family=xunit1
```

ここでは全五条件と全四候補、20構成を照合する。
有効時50件・無効時0件という結果だけでなく、計測10件・表示2frame・確定PPS値が保たれるかを確認する。
別のperiodを試す場合は、実験ソースと独立oracle・期待回数の変更を分け、旧条件の記録を上書きしない。

## 結果と照合する

[容量表](x0-features-size-matrix.csv)で同じarch・LTO・候補を選ぶ。
`off` と `manual` は無効featureの仕組みの費用、`on` と `off` は機能を足す費用を比較する組。
`runtime0` と `off` は能力とlayoutも異なるため、差の全てをflag一個の費用にはしない。

必要な状態を、入力payload／session、表示・計測buffer、タッチbuffer、周期・pending、
busの所有と終端、実行器・通知、検査counterに分けて列挙する。
`.su` の関数frameとstatic RAMを足しただけで、実機のstack peakを得たことにしない。

## 人による評価で記録すること

予測で漏れた状態、変更時に触った箇所、失敗時の理解、何を待つと処理が止まるかを記録する。
正解を先に見た後の説明だけで、教材が理解を助けたと評価しない。
タッチなしの小用途でもこの型選択の説明が必要か、便利さと説明量のバランスをP4のレビューへ渡す。
