# EXP-04 RAWとblobと数値keyの条件付き比較

2026-10-20。試作commit `4d91ac7`。
[媒体・領域準備](region-result.ja.md)の5 geometryに対して7候補を作り、予約1／2／4消去単位で比較した。
実装対象はhost上の形式模型で、製品core・USB・製品linkerは変更していない。
公開API、採用する保存形式、既定容量は未決定。MCUのcode／RAM費用・停止時間・実電断は未測定。

## 固定した比較条件

[形式入力](../../flash-format-inputs.json)に形式・保護条件・注入集合を固定し、[物理単位の入力](../../flash-inputs.json)を使う。
再programと中断範囲が未確定なため、今回も媒体の条件付き感度分析として扱う。
独占・直列の領域操作は呼出し側の前提で、RAWとkeyの同時所有を止めるlease実装はまだ含まない。

| 候補 | 形式 | 更新時の操作 | 完了印 |
|---|---|---|---|
| raw | 既知長8 B、検出情報なし | 全予約領域を消去して書く | なし |
| blob-overwrite | 8 B blobとCRC header | 1領域を消去して書く | なし |
| blob-two-bank | 同じblob形式 | 非active側を消去して書く | なし |
| keys-overwrite | 最大2件の数値key snapshotとCRC header | 1領域を消去して全snapshotを書く | なし |
| keys-two-bank | 同じkey形式 | 非active側を消去して全snapshotを書く | なし |
| blob-two-bank-commit | blobとCRC header | 非active側のbody後に別program単位へ完了印 | あり |
| keys-two-bank-commit | key snapshotとCRC header | 非active側のbody後に別program単位へ完了印 | あり |

2領域候補は等しいbankを2つ必要とし、1消去単位の指定は受理しない。
1領域候補には旧値を残す保証を付けない。今回のRAWは置換対照であり、append用RAW API全般の能力を表すものではない。
keyはuint16識別子とuint32値を1件6 Bで表し、識別子の昇順・重複なしで格納する。
文字列key、namespace、可変blob値、Preferences互換を実装したものではない。

CRC付きheaderは16 Bで、magic、32 bit世代、payload長、版、CRC32を持つ。
CRCは先頭12 Bとpayloadを対象とし、独立したbit演算参照と既知の検査値でも照合した。
完了印の内容は12 Bで、tag・世代・headerのCRCを持つ。bank末尾の別program単位へ最後に書く。
program単位へ切上げた余白は0xffで埋め、同じprogram単位へ後付けで再書込みしない。

読出しはCRCとschema、必要なら完了印を検査して、有効な最大世代のbankを選ぶ。
等しい世代が2つあれば曖昧として拒否し、32 bit世代のwrapも拒否する。
不明・破損形式を見つけても自動消去せず、明示的な置換指定を要求する。
置換は失われた旧値を復元する操作ではない。

## 明示的な進行と再走査

beginでhost上の書込み内容をsnapshotし、stepごとに模型のeraseかprogramを1回だけ行う。
key変更も更新中に別keyを失わないよう、読めたsnapshot全体を置き換える方式とした。
途中のabandonはvolatileな進行状態だけを捨て、rollbackしない。
中断後は新しいscannerで同じ媒体を読んでから、次の更新を開始する。

この模型はPythonのlist・bytes・dictを使い、bank全体の読出しや操作列もhostに保持する。
そのmemory量や1回の関数時間を、小型MCUの実装費用として使わない。
stepが1媒体操作で戻ることも、実Flash操作中にCPUやIRQを止めずに済む意味ではない。
実装を移す段階で、streaming、作業buffer、利用者bufferの寿命、完了・失敗・回収を別に測る必要がある。

## 予約量と形式の差

V003 fastの消去・program単位64 B、blob 8 B、key最大2件12 Bを例にする。

| 条件 | 予約 | 1 bank | header | 完了印の割当 | 設定したpayload | 収まるか |
|---|---:|---:|---:|---:|---:|---|
| raw、1単位 | 64 B | 64 B | 0 | 0 | 8 B | はい |
| blob上書き、1単位 | 64 B | 64 B | 16 B | 0 | 8 B | はい |
| key上書き、1単位 | 64 B | 64 B | 16 B | 0 | 12 B | はい |
| blob／key、2 bank、1単位 | 64 B | 構成不可 | — | — | 8／12 B | いいえ |
| blob／key、2 bank、2単位 | 128 B | 64 B | 16 B | 0 | 8／12 B | はい |
| 完了印付き2 bank、2単位 | 128 B | 64 B | 16 B | 64 B | 8／12 B | いいえ |
| 完了印付き2 bank、4単位 | 256 B | 128 B | 16 B | 64 B | 8／12 B | はい |

完了印の内容が12 Bでも、独立してprogramする条件では64 Bを割り当てる。
standard programの2 B単位なら完了印割当は12 B、X035 fastの256 B単位なら256 Bとなる。
形式の内容量とprogram／eraseの割当量を分けないと、この差を見落とす。

全105行では81条件が設定したpayloadを受理し、24条件が媒体を変更せず容量不足を拒否した。
CSVの`max_payload_bytes`はbankからheader・完了印割当を引いた形式上の上限で、現在設定したblob長やkey件数ではない。
例えば64 B bankの上限48 Bから、今回のkey上限2件を自動的に8件へ増やすことはない。
schemaや型を変えたときの費用と操作条件は、別の比較として扱う。

## 中断と再更新の結果

各成立条件で第1・第2版を保存した後、第3版への更新を対象とした。
第3版の開始前と各媒体操作の終了後、各操作中の6種類のbyte maskと前半byteだけの変更を注入した。
同じ媒体履歴を保持した新scannerで読出し、次の設定値への再更新まで確認した。
初回保存中の中断、実reset、隣接領域に及ぶ障害、全bit組合せの網羅ではない。

| 候補 | 成立する容量条件 | 中断条件数 | 無効値／未検出の別値 |
|---|---:|---:|---:|
| raw | 15 | 487 | 415件は既知の旧値／新値以外。形式自身は未検証のbyte列として返す |
| blob-overwrite | 15 | 679 | 607件がemptyまたはcorrupt |
| keys-overwrite | 15 | 727 | 655件がemptyまたはcorrupt |
| blob-two-bank | 10 | 386 | 今回の集合では0件 |
| keys-two-bank | 10 | 418 | 今回の集合では0件 |
| blob-two-bank-commit | 8 | 496 | 今回の集合では0件 |
| keys-two-bank-commit | 8 | 528 | 今回の集合では0件 |

合計3,721条件を記録した。2 bank候補の1,828条件では有効な旧値または新値を読めた。
上書きでは、消去完了からprogram開始までに旧値が失われる具体例も検査した。
破損時の次回更新には明示的な破損領域の置換を使い、世代を再初期化して全3,721条件で再更新を完了した。
旧値が戻ったと、更新できる状態に戻ったことは別である。

これらの数は故障率ではない。候補ごとに操作数・中断箇所が異なる。
CRCの衝突、影響範囲、電圧・時間条件を限定できていないため、電断耐性の一般証明にもならない。
CRCだけの候補も完了印付き候補もこの集合では旧値／新値となったが、実機での保護能力が同じと結論しない。
完了印による操作順と追加費用を比較できた段階である。

## 証拠と再実行

hostは形式200件、従来のgeometry／媒体138件、workspace3件の**341件**が成功した。
不正key、CRCが合っても不正なschema、世代上限、同世代の曖昧さ、busy、明示置換、key削除、入力snapshotも含む。
conftestは追加せず、禁止ルールも維持した。

[容量と操作回数のCSV](format-size-matrix.csv)、[入力・tool・全traceとarchiveのJSON](format-measurements.json)を保存した。
archive内の`cut-traces.jsonl`に個々の条件・結果・再更新の成功を保持している。
MCU code bytes／RAM bytes／stall timeは未測定としてJSONで`null`にした。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_formats.py \
  tests/unit/core_redesign/test_flash_regions.py tests/unit/core_redesign/test_flash_model.py \
  tests/unit/test_workspace.py -o junit_family=xunit1

python3 tests/platform/core_redesign/compare_flash_formats.py \
  --out work/exp04-format-recheck
```

比較記録の出力先には未作成の場所を使う。

## 次の通常作業と上位判断

後続の[初回中断・C++／ABI比較](store-result.ja.md)を記録した。
以下のうちC++試作と初回の有限中断集合は検査済みとし、走査分割・実媒体・別形式の比較は続ける。

通常作業では初回保存の中断、型付きC++試作とABI・未使用除去、実媒体境界と所有・buffer寿命、
満杯・追記・整理・write amplification・作業RAMを追加比較する。
毎回bank全体を消去する今回のsnapshot方式は、小更新の耐久性を評価し終えた設計ではない。
文字列key／namespace／可変長値、schema移行、より小さいmetadata、CRCや完了印の構成差も候補に残す。
これらを今回の対象外に決めたわけではない。

実Flash driverでは再program、物理中断範囲、error／readback、CPU／IRQ停止、電源・耐久性を確認する。
upload時の保持、保存中の通信・PDとの共存、実電断はそれぞれ別に測る。
P5では必要な保護契約、RAWの公開範囲、keyの範囲、容量指定・既定値を判断する。
[更新途中を予測する課題](format-exercise.ja.md)を教材素材に加え、人が理解・変更できるかの評価は残す。
