# 保存を受理する前の走査と所有を追う

[統合実験](integrated-result.ja.md)を使う教材案。利用者が予測・変更できるかは未評価。

1. 空の4 KiB領域を、1回32 byteのpollで全消去済みと確認する。必要な呼出し回数を計算する。poll(0)と、pollへ最大値を渡した場合も予測する。
2. Readyのkey mapを書き換える。putの中で媒体を再走査しなくてよい理由と、その前提が破れる操作を挙げる。
3. begin_update成功後に元の入力arrayを変更する。保存に使われる値と、peek／getの成否を予測する。
4. write_stepがDoneになった。次のputを始める前に必要な操作と、古い値を返さない理由を説明する。
5. programが失敗したが、全bitが新値になっていた。Failedを「旧値が残る」と解釈すると何を間違えるか考える。
6. Readyのまま長い処理をする。台帳を共有する別writerがいつ領域を使えるかを説明する。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_integrated.py -q
```

正常更新の検査では、受理直後に入力vectorが破棄されても、writerのcopyから期待するbyteが保存される。
予算・中断・所有喪失の検査と、自分の予測を照合する。
write_step 1回が短時間とは限らず、read API 32回も32 CPU cycleではない。

変更課題では、Readyの間だけ別ownerに領域を渡す方式を考える。
再取得後に前のsnapshotを使ってよいか、新しいticketだけで内容の不変性を保証できるか、再走査はどこで行うかを先に書く。
元の長期所有と同じ意味だとせず、追加状態・読出し回数・他ownerの待ち方を比較する。
最後に同期側と統合側のRAM差を表から読み、台帳44 Bを利用者ごとに重複計上しないよう内訳を示す。
