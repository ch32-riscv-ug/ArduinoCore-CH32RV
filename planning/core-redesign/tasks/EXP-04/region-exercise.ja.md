# 保存領域の容量と保持条件を考える課題

2026-10-20。利用者が小型MCUの資源を意識して設計できるかを調べる教材素材。
人による予測・変更の評価は未実施。[結果と条件](region-result.ja.md)を参照する。

## 読む量と書く量を分ける

V003で設定が10 Bある場合を考える。byte readができても、同じ単位でFlashを書き換えられるとは限らない。
高速modeでは64 Bのprogramと64 Bのerase、standard modeでは2 Bのprogramと1,024 Bのeraseを比較する。
高速bufferに4 Bずつloadできることを、Flashへ4 Bずつ保存できる意味に読み替えない。

次の量を分けて予測する。

- 予約するFlash量と、その分減るアプリ容量。
- 10 B以外に必要な形式情報、検出情報、更新・整理用の余地。
- 更新時に読み出して保持するbyteと、必要な作業RAM。
- 保存途中でresetしたとき、検出できることと旧値を復旧できること。

まだ保存形式を選んでいないため、この課題の生成器は有効payload量を未確定として出す。

## 1消去単位の予約を変更する

まず実行前に、1単位と4単位でアプリ容量が何byte変わるかを計算する。

```sh
python3 tests/platform/core_redesign/flash_regions.py \
  --part CH32V003F4P6 --mode fast --erase-units 1 --out work/store-64

python3 tests/platform/core_redesign/flash_regions.py \
  --part CH32V003F4P6 --mode fast --erase-units 4 --out work/store-256
```

`partition.json`の`reserved_bytes`、`app_bytes`、`program_slots`を確認する。
1単位時は予約64 B・アプリ16,320 B、4単位時は予約256 B・アプリ16,128 Bとなる。
program枠数を「設定を安全に更新できる回数」と解釈できるか、追加条件を挙げる。
同じ指定をstandard modeやX035へ変更し、同じ1単位でも何が変わるか説明する。

`menu-candidates.json`は0／1／2／4単位の比較用データ。製品menuや既定値の採用結果ではない。
出力先には新しい場所を使い、既存記録を上書きしない。

## 消去とuploadを区別する

| 状況 | 予測すること |
|---|---|
| `.persistent`をNOLOADへ置く | imageから省かれることと、upload時に消されないことは同じか |
| uploaderがmain Flashを全消去する | binに保存領域がなくても、旧設定は残るか |
| uploaderがアプリ末尾を含む1 KiBを消す | 保存予約が64 B整列であれば十分か |
| 保存領域へ初期値付き変数を置く | 初回値・更新値・通常uploadでの扱いをどう分けるか |
| link origin 0と物理origin 0x08000000がある | erase範囲を照合するとき、どちらのaddressを使うか |

今回のprobeは初期値の混入をlinkで拒否する。実uploaderの消去単位・address mapping・mass eraseの挙動は別に調査する。
この点が分からないまま「NOLOADなので保持」と説明しない。

## 中断後の0xffをどう解釈するか

媒体模型では、一度programを試みた単位は、見かけが全て0xffでも再programできない制限を置いている。
これは安全側の比較条件で、実chipの再program規則を確定したものではない。
消去が途中で止まった場合も、完了したeraseをやり直すまで新規のprogramを拒否する。

この模型の試行履歴はhost側の非永続状態であり、電源投入時に実Flashから自動で分かる情報ではない。
復旧処理が必要な場面で、利用できる観測と、単なる仮定を分けて説明する。
指定bitだけの模型試験と、実電断で影響する範囲の確認も分ける。

次の変更課題では、1領域での単純な保存、更新中の破損検出、複数領域での旧値保持を別の条件で比較する。
容量を増やしただけで保護能力も増えたと決めず、形式・操作順・回収条件を説明してから実験する。
