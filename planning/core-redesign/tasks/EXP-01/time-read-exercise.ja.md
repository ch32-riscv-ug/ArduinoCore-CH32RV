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

## 論理時刻と観測していない時間

[論理時刻の比較](logical-time-result.ja.md)で、6 MHzの1 tickを6回観測する場合を予測する。
端数を持てば合計1 µsへ進むが、そのための状態が必要になる。
次に32 bitの差が6 tickだったとき、実際は2^32＋6 tickだった可能性を値だけで排除できるか説明する。
観測上限を同じcounterの差から作っただけでは、その上限を独立に確認したことにならない。

64 bitのsourceがsleep中に止まった場合、rawが同じままでも実時間が進んだ可能性がある。
外部から経過を与える場合と、分からないまま新しい論理epochへ移る場合を分けて考える。
3 Hzで1 tick観測した後、7 Hzへ切り替えると、整数µs以外に何の端数が残るか計算する。
その端数を捨てる許可と、捨てた量の報告が必要な理由を説明する。

## timerと費用の予測

予定がある状態で論理epochが変わった場合、未来の予定と既に保留している通知を分けて予測する。
今回のadapterは未来を停止し、旧通知は旧epoch付きで残す条件を検査した。採用方針ではない。
32 bit timerへ64 bit時刻の低位だけを渡すとき、観測空白が半周以上だったことをどこで検出するかも確認する。
2つの別clock objectでepochが同じでも、同じ時間domainとは限らない。

費用表を見る前に、差分が32 bitと分かっていても64 bitの除算・剰余を使った場合の追加codeを予測する。
[同じ契約の変更前後](logical-time-optimization.csv)と[最終構成](logical-time-size-matrix.csv)を照合する。
V003の6 MHz・32 bit条件は3,592→904 Bとなったが、これは全API入口・補助コードを含む特定条件である。
Clock本体の32 Bと入口のRAM、単独frameとstack peakを分け、単純なbit幅だけで速さや容量を決めない。
時刻を読めたこと、期限を確認したこと、callbackを実行したことも別の処理として説明する。
