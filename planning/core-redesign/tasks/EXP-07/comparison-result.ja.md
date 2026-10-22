# N07 変更課題の統一記録（4件、切断復旧は未完了）

2026-10-22。実験・集計実装 `ab5913a`。
[共通項目](../../change-exercises-inputs.json)、[集計条件](../../change-exercises-records.json)、
[機械比較JSON](comparison.json)、[測定manifest](change-exercises-measurements.json)、[課題案](exercise.ja.md)。
同じ形式でbaseline／変更版／編集file／共通準備／追加状態／意味／trace／ABI／未検証を記録した。
4件分の96 ABI pairを整理したが、N07全体の終了ではない。E05切断復旧追加は未実装。

## 各課題の範囲

| ID | 選択ファイルの変更 | 追加・削減する状態 | 意味・前提の変化 | 証拠 |
|---|---|---|---|---|
| E01 K0設定2件化 | [1件](k0-two/before.cpp)→[2件](k0-two/after.cpp) | protectedなK0Frame8 Bが1件増える。staging・read snapshot・executor枠は同じ | 処理前に2件目を保持でき、拒否される境界が変わる。ACK-discardとdata-NACKの違いは維持 | [既存slave接続結果](../EXP-06/k0-slave-register-result.ja.md)、32 ABI pair |
| E02 表示交換 | [Mono64](display-change/before.cpp)→[Compact32](display-change/after.cpp) | chunk上限32→31、Viewとfrozenは同じ。全frameを常設保存しない | mock frame1024→512 B、status位置とwire量・遅れが変わる。物理panel交換ではない | [既存表示結果](../EXP-05/display-result.ja.md)、16 ABI pair |
| E03 タッチ追加 | [off](touch-add/before.cpp)→[on](touch-add/after.cpp) | buffer8 B、3 counterとpending、bus ownerのcounter／失敗枠 | 8 Bを50 Hzで共有busへ追加。実行時on/off機能ではない | [既存機能除去結果](../EXP-06/x0-features-result.ja.md)、16 ABI pair |
| E04 queue削減 | [4件](queue-change/before.cpp)→[1件](queue-change/after.cpp) | 入力envelope3件、静的RAM24 B減。新しいapp stateなし | reject-newを維持しburst許容量を減らす。applyが拒否される例を追加 | [今回の結果](queue-change-result.ja.md)、32 ABI pair |
| E05 切断復旧追加 | 未実装 | 未評価 | 既存detach／fault成功を追加実装の課題完了に置き換えない | 次のN07作業 |

E01〜03は既存の実行・測定を再利用した。今回新しくhost／ABIを全件再実行したという意味ではない。
既存matrixのdigestまたはmanifest内の全rowと照合し、旧archiveとともに保全した。
新しい6選択fileは、4候補すべてで過去に測定した型と一致することをsyntax検査6件で確認した。
E01のCはC-bridge、E02／E03のCもその実験で定めた固定条件であり、全てを汎用event配信Cとみなさない。

## 数値を比較する単位

各pairのarch／part、LTO、候補、残りの条件を固定する。実験ごとのentry・harness・能力が異なるので、課題間の絶対値を順位付けしない。
E03の旧表はarch単位かつM-only linkで、他の表の具体part／M/S/U entryと同じ全体firmwareではない。
以下は各表内のR、LTOあり、V003またはそのRV32E構造profileの差。

| ID | image差 B | 静的RAM差 B | 注意 |
|---|---:|---:|---|
| E01 | +32 | +8 | ACK-discard条件。別policyを混ぜない |
| E02 | +48 | 0 | frame量は半分でもsizeofは整列で同じ |
| E03 | +212 | +28 | raw field量、object整列、bus枠を分けて読む |
| E04 | −48 | −24 | touchなし。入力拒否が増える条件もある |

各変更の選択差分は論理file1個・1行追加／1行削除。
この値は用意した選択API上の差であり、利用者が自力で行う編集量・理解度・所要時間の実測ではない。
E04には容量化headerの共通準備も追加し、そのfile・byte数・行数を別に記録した。
他の3件も既存の部品・adapterが完成していることを前提にしており、framework自体を作る費用を0と評価しない。

## 統一集計の検証

`tests/platform/core_redesign/summarize_change_exercises.py --out work/<new-directory>` で再生成する。
一対一のpair、各課題の期待pair数、4候補の網羅、元matrixとの照合を検査する。
E01のC-bridgeを落とす条件と、異なるmatrixへの差替えは負の検査で拒否された。

集計初回は表示表のempty rowをbeforeへ混ぜて重複を検出した。
次の集計でC-bridgeの名前差によりE01が24 pairとなり、監査で不足を確認。32 pair／4候補を必須にした。
最終r4はE01=32、E02=16、E03=16、E04=32、E05=pending。
選択file検査の初回はhash helperのstr／Path誤りで6件失敗し、修正後6件成功。課題のprototypeはその期待へ合わせて変更していない。

今回の新しいqueue host検査は39件、workspace3件、型選択6件。queue ABI196 build。
既存の成功件数を新規実行数へ足さない。人の成績・誤解・支援回数・予測能力は全件nullで残す。

## 残る作業

E05は機能のないbaseline→切断復旧を足した版の実装・差分・追加状態・意味・trace・ABIが必要。
E04のresident retry producer、物理IRQ／wire、実stack／時間も未検証。
人には5課題すべてについて予測→編集→照合を依頼するが、そのレビューをこの機械比較で代替しない。
N07を続け、全体予算・実runtime等はN08、実機はN09、公開範囲と採否はP5へ渡す。
