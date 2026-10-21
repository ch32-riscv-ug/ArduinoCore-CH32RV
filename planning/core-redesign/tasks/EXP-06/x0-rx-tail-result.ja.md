# N02 X0用途の受信末尾・停止待ち・終端後の故障

2026-10-21。実装commit `e7ad327`。入力は [x0-rx-tail-inputs.json](../../x0-rx-tail-inputs.json)。
host刺激と検査だけを追加し、製品コア、実験用firmware header、既存driverは変更していない。
新しいhost driverは前回のfixtureを基に受信境界の刺激を追加したもの。
比較対象は[直接回収](x0-register-wire-result.ja.md)と[通知による回収](x0-event-wire-result.ja.md)の同じX0用途。
方式の採用、N02／P4全体の終了ではない。

## 固定した刺激と期待結果

先に5000 mV／1000 mAの有効なpairを公開し、次のbus wordまたはcurrent wordの取引で故障を入れる。
raw入力は18000／65506で、後続の正常pairなら9000 mV／−30 mAとなる模型。
実変換の鮮度・精度・calibration成立を示す値ではない。
error flagはBERR／ARLO／AF／OVR／PECERRの5種類を個別に注入した。

| 注入点 | Data registerのread数 | Completion.received | 終端結果 |
|---|---:|---:|---|
| pointer送信後、repeated START前 | 0 | 0 | Failure |
| RX ADDR後、FIFO空 | 0 | 0 | Failure |
| FIFOに1 byte、pair read前 | 1 | 0 | Failure |
| FIFOに2 byte、pair read前 | 2 | 0 | Failure |
| 2 byte read後、quiet前 | 2 | 2 | Failure |
| quiet／reap／close後 | 2 | 2 | 確定済みSuccessのまま |

ここでFIFOに残っていたbyteは故障後の停止処理で排出される。
排出のためにDataを読んだことと、有効な受信prefixとして数えたことを区別した。
特に1 byteのFIFOを「1 byte受信成功」とは扱っていない。
既存RX2 engineは2 byteを同じguarded処理内で読むため、今回はその二つのread命令間へ割り込んでいない。
実IRQ・電気的な故障の全interleavingの証明ではない。

2 byte read後でも、停止前のerrorはFailureとなる。
直接・通知の両方式とも、用途takeが失敗結果を回収した時点で旧5000 mV／1000 mAを保持しstaleにする。
受信byte数が要求量に達したことだけで、新しい値や計測payload8 Bを公開しない。
終端後の注入ではportのpollがMMIOを行わず、確定済み結果とflagsを書き換えなかった。
後続の正常入力では新しいpairへ回復した。

## 停止待ちと通知停滞

追加のhold条件では、2 byteを読んでStopに入ったまま、START／STOP・BUSYのquiet刺激を与えない。
時刻や用途step、pumpだけを進めても終端にならず、leaseとstorage loanを保持した。
明示quiesceの後にleaseを返しても、用途takeまではloanが残る。
read後にraw sourceを変えてからquiesce／takeし、用途が後のsourceを読まず既に受信したbyteを使うことも照合した。

通知方式では、read済みだがFailureの結果をFIFO満杯のまま保持し、宛先解除・再bind・retryを実行した。
受理中の通知が失われても結果は残り、配達と用途takeの後にFailure／staleを反映した。
これはsourceとcallback contextが生存している固定宛先の実験で、動的破棄のIO-04を検証したものではない。

## 検査と証拠

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_rx_tail.py tests/unit/test_workspace.py -q
```

native70件＋workspace3件、計73件成功。既存plugin由来warning146件。
2種類の量×6故障点×5 flagを、direct／event・R/A/B/Cへ適用した。
さらに通知満杯の2条件、quiet待ちの4条件、従来wire命令による正常用途4条件を検査した。
各テスト内のmode／候補実行をテスト件数へ重複加算しない。

512 traceを保存し、mode・入力ごとの128群でR/A/B/Cの全観測行が一致。
入力操作1,667,916件、MMIO／critical event 10,334,656件は保存・照合の件数であり、処理速度の指標ではない。
独立whole-image／transaction模型を使い、各入力後の用途・通知の状態を照合した。
故障点ごとのData read値・有効受信数・flags・旧payload不変もliteralで照合した。
乱数検査は今回追加していない。前回のseed付き検査を新しいtail刺激の検証済み範囲として数えない。

全対象firmware headerは前回ABI実験のhashと一致した。
既存68構成の記録と全入力hashを参照し、**新規ABI buildは0件**。
host専用macroをtargetへリンクした費用は存在せず、前回のFlash／静的RAM値へ架空の追加費用を足さない。
同時に、以前のサイズ測定を今回の新規測定件数へ加算もしない。
時間・IRQ禁止区間・stack peak・物理STOPの成立は引き続き未検証。

[測定記録](x0-rx-tail-measurements.json)にnative入力・tool・JUnit・trace・前回ABIへの参照とarchiveを保存。
移送archiveは `work/exp06-x0-rx-tail-20261021-evidence.tar.gz`、11,920,896 B、
SHA-256 `4025113026b26fea03b42b40474502b8e729ef81e9d554cc45bfb3e1d0f0c750`。
`EXP06_X0_RX_TAIL_EVIDENCE` は未作成directoryを指定する。
今回も短い専用TMPDIRを使用し、conftestは追加していない。remote CI成功ではない。

## 次の接続

N02ではX035の周辺準備adapterを用途へ接続する作業が残る。
nominal clock、route、package、pin設定の準備と再確認を、boolean ready宣言から置き換えて比較する。
そのhost／link成功も実clock・実pin・実I2C成立とは区別する。
実機・変換品質・公開callback寿命・利用者レビュー・P5の採否は未確定。
[教材案](x0-rx-tail-exercise.ja.md)の作成を、人の理解しやすさの評価にはしない。
