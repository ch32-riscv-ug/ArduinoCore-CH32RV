# 所有を返すまで再利用しない：変更課題

2026-10-21。[結果](h417-ownership-result.ja.md)と実験用`test_h417_ownership.py`を使う。
製品コア・USB・実機は変更しない。先に予想を書き、traceと比較する。人による実施・理解度評価は未実施。

1. 2 blockをreserveして、generation2を先にpublishする。takeの順、通知ack直後にreserveできるか、2個takeした後にreserveできるかを予測する。
2. 先にtakeしたblockを最後に返す。返却した1個だけを再利用し、古いtokenでread／releaseを試す。新しい借用を解放できない理由を説明する。
3. messageを5個pushして通知をackする。messageの受理数と、その後4個popできるかを予測する。通知を配達した件数と実データを回収した件数を区別する。
4. blockの17 byteまで書いてrecoverする。producerとconsumerだけ静止を宣言しrestartする。その後DMAも静止を宣言してやり直す。成功後の古いtokenと、新しい完全publish後の読出しを比較する。
5. 4について「timeoutしたからDMAも静止扱い」に変えてよいかを考える。checked APIに現れない旧DMA書込みをこのhost実験が検出できるか、実機で何を観測すべきかを書く。
6. shared-accessでcore1が生成したblockをcore0へ渡す。core1からの返却とcore0からの返却を比較する。「どちらからも開始可能」と「借用中のownerなし」の違いを説明する。
7. 結果表の236 Bからpayloadを引く。fake Gateと測定harnessも含めた総RAMを求める。messageだけ使う場合を今回の値から断定できるかを検討する。

counter感度条件の8 bit枯渇は短時間で再現するための設定。
拒否をwrapへ変更すると古いtokenがいつ同じになるか、外部domainとstorage生存期間の条件も含めて説明する。
実験値から方式の採用を決めず、理解に必要だった状態・trace・変更箇所をP5のレビュー材料として記録する。

[機能除去の追加実験](h417-features-result.ja.md)では、messageのみ・blockのみ・両方・回復管理のみを比較する。
本体byte数とharness込みのRAM差が一致しない理由を探す。
また、bothのimageが専用試作より増えた場合でも、除去可能性を評価するにはどの用途を比べるべきか考える。
