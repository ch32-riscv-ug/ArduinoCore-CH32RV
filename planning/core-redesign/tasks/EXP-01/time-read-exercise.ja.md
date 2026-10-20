# 変更課題：生counterと経過時間を区別する

[clock/time実験](clock-time-result.ja.md)の教材素材。公開API案の採用や、人による教育効果の確認は未実施。

## 数値表を見る前に予測する

1. clockのready待ちを短いstepへ分けても、一定時間内にreadyになる保証は得られるか。
2. CounterのReadyは「sampleを読めた」と「時間が進み続ける」のどちらを示すか。
3. V003で7 tickと2^32＋7 tickを同じ値として読む場合、呼出し側が経過時間を復元できるか。
4. 6MHzの1 tickを6回microsecondへ変換して加算する結果と、6 tickを一度に変換する結果は同じか。
5. counterを停止・再開始した後、古い開始sampleを新しいsampleと引き算してよいか。
6. 64 bitのcounterを32 bit CPUで読むとき、high／lowが別の瞬間の値になる可能性をどう扱うか。

## 同じ意味で比較する

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_clock_time.py -o junit_family=xunit1
```

生count、epoch、周波数、経過tick、変換済み区間を別の情報として追う。
whole-wrapの負の例と、fractionを捨てる例を見て、何の情報が不足しているかを説明する。
それを補うために、保持する状態、呼ぶ頻度、IRQや別の時刻源など、どの条件が必要になるかを列挙する。
方式の採用はこの課題では決めない。

## 費用を照合する

[容量表](clock-time-size-matrix.csv)で同じpart・LTOを選び、
低32 bit、系列のnative幅、可変rateの変換、定数rateの同じ式、特化した32 bit式を比較する。
「定数を渡したので必ず小さくなる」「64 bitの値ならwrapを意識しなくてよい」と予測していなかったかを確認する。

特化式はrateと入力範囲が固定され、counterの複数wrapやfractionを自動処理しない。
小さいFlash量だけを根拠に、それを連続時刻やArduino micros()の代わりにしない。
Flash、static RAM、stack予約、未取得のstack peakと実時間を区別して記録する。

## 人による確認で残すもの

予測との差、見落とした状態、必要なsample間隔を説明できるか、失敗後に何を再初期化するかを記録する。
実装や説明を見た後の再現だけで、利用者が自分で分割・変更できることの証拠とはしない。
